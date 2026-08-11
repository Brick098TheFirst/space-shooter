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
