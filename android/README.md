# Space Unlimited: Recharged — Android (native host)

Android compiles the **same C game** in `gba/` with the NDK (`PLATFORM_HOST`). It is not a WebView, not an emulator, and not a second remake.

GBA-only hardware (Mode 4 VRAM, DirectSound DMA, SRAM at `0x0E000000`) is swapped in `platform.h` / `platform_host.c`. Gameplay, shop, renderer software blit, and mixer stay in the shared sources.

### Android Host Features

- **16:9 Widescreen Presentation (284×160):** Pixel-perfect widescreen viewport expanding the field of view while preserving authentic pixel art scale and responsive UI layout.
- **90 Hz High Refresh Rate:** Configured for 90 Hz display refresh mode with synchronized 90 FPS Choreographer frame loop and 90 Hz time-accumulated physics.
- **Synchronized Audio Mixer:** Mixes 202 samples @ 90 Hz (18.157 kHz source) and resamples to 44.1 kHz so Android does not pitch-shift or boom the soundtrack. Menu BGM is ducked so it is not louder/faster than the in-game track.
- **Native Menu Touch:** Menus, hangar, upgrades, pause, and game-over are tapped directly. Shop tabs (`PAINTS` / `TRAILS` / `WEAPONS` / `LASERS`) use full-width touch zones, item/buy taps, and a horizontal swipe to change category. The virtual stick is hidden outside gameplay.
- **Game Modes (Android only):** Play opens a mode select. **Waves** is the classic clear-the-wave run. **Endless** never stops — threat ramps, hunters spawn at random, no wave banners. **Overdrive** is a 90-second score rush with denser random spawns.
- **Circular Virtual Stick:** In-game only floating circular analog pad (left) plus circular FIRE / BEAM (hold for big laser) buttons and a PAUSE chip.
- **Tilt Steering & Haptics:** Settings menu toggles accelerometer steering (phone tilt moves the ship when the stick is idle) and vibrator feedback (hits, beam charge, beam fire). Both persist in the save file.
- **Dynamic Phone Scaling:** The 284×160 framebuffer letterboxes to the current window size and relayouts on rotate / multi-window resize.
- **Persistent Storage:** Coins, owned loot (paints, trails, weapons, lasers), upgrades, and high score write to `files/saves/save.sav` under the app's private internal storage (`Context.getFilesDir()`). That directory is always readable and writable by the app with **no storage permission and no folder picker** at launch. Legacy `space_unlimited.sav` files are migrated automatically.

## Build

```bash
cd android
./gradlew assembleDebug
```

APK: `android/app/build/outputs/apk/debug/app-debug.apk`

Requires Android SDK + NDK (CMake). The GBA ROM build (`npm run build`) is unchanged and still uses libtonc (240×160 @ 60 FPS).
