import subprocess
import numpy as np
import time
import requests

RATE = 44100
CHANNELS = 2
CHUNK = 4096

ESP32 = "http://10.57.46.67/music"

BASS = (30, 250)
MID = (250, 2000)
TREBLE = (2000, 10000)


def get_default_sink():
    return subprocess.check_output(
        ["pactl", "get-default-sink"],
        text=True
    ).strip()


def start_capture():
    sink = get_default_sink()
    monitor = sink + ".monitor"

    print(f"\n🎧 Capturing: {monitor}")

    proc = subprocess.Popen(
        [
            "parec",
            f"--device={monitor}",
            "--format=s16le",
            f"--rate={RATE}",
            f"--channels={CHANNELS}"
        ],
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL
    )

    return proc, sink


def band_db(freqs, power, low, high):
    mask = (freqs >= low) & (freqs < high)

    if not np.any(mask):
        return -80.0

    energy = np.mean(power[mask])

    return 10.0 * np.log10(energy + 1e-12)


def adaptive_normalize(value, history):
    history.append(value)

    if len(history) > 150:
        history.pop(0)

    if len(history) < 20:
        return 0.0

    low = np.percentile(history, 10)
    high = np.percentile(history, 90)

    if high - low < 1:
        return 0.5

    return float(np.clip(
        (value - low) / (high - low),
        0.0,
        1.0
    ))


bass_history = []
mid_history = []
treble_history = []

bass_smooth = 0.0
mid_smooth = 0.0
treble_smooth = 0.0

bass_ema = None
last_beat = 0

proc = None
current_sink = None

session = requests.Session()

try:
    while True:

        # Automatically follow speaker ↔ Bluetooth changes
        sink = get_default_sink()

        if proc is None or sink != current_sink:

            if proc is not None:
                proc.terminate()
                proc.wait()

            proc, current_sink = start_capture()

        raw = proc.stdout.read(CHUNK * CHANNELS * 2)

        if len(raw) < CHUNK * CHANNELS * 2:
            continue

        samples = np.frombuffer(raw, dtype=np.int16)

        samples = samples.reshape(-1, CHANNELS)

        # Stereo → mono
        samples = samples.mean(axis=1)

        samples = samples / 32768.0

        # Remove DC offset
        samples -= np.mean(samples)

        # FFT window
        samples *= np.hanning(len(samples))

        fft = np.fft.rfft(samples)
        power = np.abs(fft) ** 2

        freqs = np.fft.rfftfreq(
            len(samples),
            1 / RATE
        )

        # Frequency bands
        bass_db = band_db(
            freqs, power, *BASS
        )

        mid_db = band_db(
            freqs, power, *MID
        )

        treble_db = band_db(
            freqs, power, *TREBLE
        )

        # Normalize 0 → 1
        bass = adaptive_normalize(
            bass_db,
            bass_history
        )

        mid = adaptive_normalize(
            mid_db,
            mid_history
        )

        treble = adaptive_normalize(
            treble_db,
            treble_history
        )

        # Smooth
        bass_smooth = bass_smooth * 0.70 + bass * 0.30
        mid_smooth = mid_smooth * 0.70 + mid * 0.30
        treble_smooth = treble_smooth * 0.70 + treble * 0.30

        # Beat detection
        if bass_ema is None:
            bass_ema = bass_db

        bass_ema = bass_ema * 0.92 + bass_db * 0.08

        beat = 0
        now = time.time()

        bass_jump = bass_db - bass_ema

        if (
            bass_jump > 5.0
            and bass_smooth > 0.45
            and now - last_beat > 0.20
        ):
            beat = 1
            last_beat = now

        # Send to ESP32
        try:
            session.get(
                ESP32,
                params={
                    "bass": round(bass_smooth, 3),
                    "mid": round(mid_smooth, 3),
                    "treble": round(treble_smooth, 3),
                    "beat": beat
                },
                timeout=0.15
            )
        except requests.RequestException:
            pass

        print(
            f"\r"
            f"BASS {bass_smooth:.2f}  "
            f"MID {mid_smooth:.2f}  "
            f"TREBLE {treble_smooth:.2f}  "
            f"BEAT {beat}",
            end="",
            flush=True
        )

except KeyboardInterrupt:
    print("\nStopping...")

finally:
    if proc is not None:
        proc.terminate()
        proc.wait()

    session.close()
