# Space Unlimited: Recharged — Android (native host)

Android compiles the **same C game** in `gba/` with the NDK (`PLATFORM_HOST`). It is not a WebView, not an emulator, and not a second remake.

GBA-only hardware (Mode 4 VRAM, DirectSound DMA, SRAM at `0x0E000000`) is swapped in `platform.h` / `platform_host.c`. Gameplay, shop, renderer software blit, and mixer stay in the shared sources.

## Build

```bash
cd android
./gradlew assembleDebug
```

APK: `android/app/build/outputs/apk/debug/app-debug.apk`

Requires Android SDK + NDK (CMake). The GBA ROM build (`npm run build`) is unchanged and still uses libtonc.
