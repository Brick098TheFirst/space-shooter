# Space Unlimited: Recharged — Android (native host)

Android compiles the **same C game** in `gba/` with the NDK (`PLATFORM_HOST`). It is not a WebView, not an emulator, and not a second remake.

GBA-only hardware (Mode 4 VRAM, DirectSound DMA, SRAM at `0x0E000000`) is swapped in `platform.h` / `platform_host.c`. Gameplay, shop, renderer software blit, and mixer stay in the shared sources.

### Android Host Features

- **16:9 Widescreen Presentation (284×160):** Pixel-perfect widescreen viewport expanding the field of view while preserving authentic pixel art scale and responsive UI layout.
- **90 Hz High Refresh Rate:** Configured for 90 Hz display refresh mode with synchronized 90 FPS Choreographer frame loop and 90 Hz time-accumulated physics.
- **Synchronized Audio Mixer:** DirectSound PCM engine automatically adjusts host buffer frames (202 samples @ 90 Hz) matching the 18.157 kHz soundtrack with zero pitch drift.
- **Virtual Touch Controls:** Ergonomic touch HUD overlay with D-pad, action buttons, shoulder buttons, and menu buttons.
- **Persistent Storage:** Flash SRAM state persists to local app storage.

## Build

```bash
cd android
./gradlew assembleDebug
```

APK: `android/app/build/outputs/apk/debug/app-debug.apk`

Requires Android SDK + NDK (CMake). The GBA ROM build (`npm run build`) is unchanged and still uses libtonc (240×160 @ 60 FPS).
