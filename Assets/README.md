# Asset provenance

These selected images and WAV files were extracted from the repository's original
`spaceshooter.sb3` project and renamed by their role in the Windows remake.
They remain assets of the original project; no third-party replacement pack is
required.

The original laser costume is preserved as `Images/laser-source.svg` and was
rasterized to `Images/laser.png` because GDI+ does not load SVG files directly.
The classic ship is colorized at runtime from the original warm wing paint; its
silhouette, cockpit and neutral trim are preserved for every paint option. The
game deliberately renders every asset at a fixed, measured gameplay size:

- ship: 66 × 50 logical pixels
- laser: 10 × 30 logical pixels
- asteroids: approximately 28–95 logical pixels across, by class
- shield: 76 logical pixels across
- explosion: scales to the destroyed object's collision size

The render canvas is 1280 × 720 and always letterboxes to 16:9, so resizing or
fullscreen mode cannot stretch the art or make objects unexpectedly huge.

The six WAV files in `Audio/` are also the checked-in source assets for the GBA
build, including the dedicated `boss.wav` cue. `tools/generate_gba_data.py`
converts them to 18.157 kHz signed 8-bit PCM arrays in `gba/src/audio_data.c`;
GBA hardware cannot play the WAV containers directly.

`Audio/story_mode.mp3` is Story Mode's soundtrack (the campaign's opening
speech, level map, dock and result cards). It is the only audio asset that is
not a WAV, so it is deliberately kept out of `tools/generate_gba_data.py`:
Python's stdlib `wave` module cannot decode an MP3, and the asset pipeline that
CI runs on every push must stay dependency-free. It has its own converter
instead:

```bash
pip install miniaudio numpy
python3 tools/generate_story_audio.py   # -> android/app/src/main/cpp/audio_story_data.c
```

The generated `.c` is committed, so a normal build never needs those packages —
re-run the script only when the MP3 itself changes. Only the Android CMake
target compiles it; the GBA ROM has no Story Mode and never links the samples.
