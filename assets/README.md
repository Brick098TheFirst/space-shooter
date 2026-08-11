# Asset provenance

These selected images and WAV files were extracted from the repository's original
`spaceshooter.sb3` project and renamed by their role in the GBA port.
They remain assets of the original project; no third-party replacement pack is
required.

The original laser costume is preserved as `Images/laser-source.svg` and was
rasterized to `Images/laser.png` for the GBA build.
The classic ship is colorized at runtime from the original warm wing paint; its
silhouette, cockpit and neutral trim are preserved for every paint option. The
game renders every GBA sprite at a fixed gameplay size:

- ship: 20 × 16 px (GBA Mode 4)
- laser: 4 × 10 / 6 × 14 px
- asteroids: 6–24 px by class
- shield: 24 × 24 px
- explosion: 24 × 24 px, 9 frames

The five WAV files in `Audio/` are the checked-in source assets for the GBA
build. `tools/generate_gba_data.py` converts them to 16.384 kHz signed 8-bit PCM
arrays in `gba/src/audio_data.c`; GBA hardware cannot play the WAV containers
directly.
