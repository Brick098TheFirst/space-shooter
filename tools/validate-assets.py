#!/usr/bin/env python3
"""Validate the selected Scratch assets without third-party Python packages."""

from pathlib import Path
import struct
import wave

ROOT = Path(__file__).resolve().parents[1] / "SpaceUnlimited.Windows" / "Assets"
IMAGES = ROOT / "Images"
AUDIO = ROOT / "Audio"

EXPECTED_AUDIO = (
    "menu.wav",
    "game.wav",
    "laser.wav",
    "explosion.wav",
    "pickup.wav",
)

EXPECTED_IMAGES = {
    "starfield.png": (956, 717),
    "classic-ship.png": (198, 150),
    "asteroid-large.png": (202, 168),
    "asteroid-medium-a.png": (86, 86),
    "asteroid-medium-b.png": (90, 80),
    "asteroid-small.png": (58, 52),
    "asteroid-tiny.png": (36, 36),
    "shield.png": (60, 60),
    "laser.png": (22, 115),
}

LOGICAL_POLICY = {
    "classic ship": "66 x 50 px",
    "laser": "10 x 30 px",
    "asteroids": "28-95 px by collision class",
    "shield": "76 px",
    "canvas": "1280 x 720 (uniformly letterboxed)",
}


def png_size(path: Path) -> tuple[int, int]:
    with path.open("rb") as stream:
        header = stream.read(24)
    if header[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path} is not a PNG")
    return struct.unpack(">II", header[16:24])


def main() -> None:
    errors: list[str] = []
    for name, expected in EXPECTED_IMAGES.items():
        path = IMAGES / name
        if not path.exists():
            errors.append(f"missing image: {name}")
            continue
        actual = png_size(path)
        if actual != expected:
            errors.append(f"{name}: expected source {expected}, found {actual}")

    explosion_frames = sorted(IMAGES.glob("explosion-*.png"))
    if len(explosion_frames) != 9:
        errors.append(f"expected 9 explosion frames, found {len(explosion_frames)}")

    for name in EXPECTED_AUDIO:
        path = AUDIO / name
        if not path.exists():
            errors.append(f"missing audio: {name}")
            continue
        try:
            with wave.open(str(path), "rb") as wav:
                if wav.getnchannels() not in (1, 2) or wav.getsampwidth() != 2:
                    errors.append(f"{path.name}: expected mono/stereo 16-bit PCM")
        except wave.Error as exc:
            errors.append(f"{path.name}: invalid WAV ({exc})")

    total = sum(path.stat().st_size for path in ROOT.rglob("*") if path.is_file())
    if total > 7 * 1024 * 1024:
        errors.append(f"selected assets are unexpectedly large: {total / 1024 / 1024:.2f} MiB")

    if errors:
        raise SystemExit("Asset validation failed:\n- " + "\n- ".join(errors))

    print(f"Validated {len(EXPECTED_IMAGES) + len(explosion_frames)} PNG files and "
          f"{len(EXPECTED_AUDIO)} WAV files ({total / 1024 / 1024:.2f} MiB).")
    print("Runtime logical-size policy:")
    for role, size in LOGICAL_POLICY.items():
        print(f"  {role:14} {size}")


if __name__ == "__main__":
    main()
