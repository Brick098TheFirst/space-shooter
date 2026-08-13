# Space Unlimited: Recharged — Android (native host)

Android compiles the **same C game** in `gba/` with the NDK (`PLATFORM_HOST`). It is not a WebView, not an emulator, and not a second remake.

GBA-only hardware (Mode 4 VRAM, DirectSound DMA, SRAM at `0x0E000000`) is swapped in `platform.h` / `platform_host.c`. Gameplay, shop, renderer software blit, and mixer stay in the shared sources.

### Android Host Features

- **Adaptive Full-Screen Widescreen:** The framebuffer is 160 px tall with a runtime width from **284 to 480 px** that matches the phone's aspect ratio, so the picture fills the whole screen edge-to-edge — **no side bars, no letterbox** — on any device from 16:9 through 21:9+. Always **landscape** (`sensorLandscape`), immersive sticky mode, and draws under the display cutout (`shortEdges`). Rotating to the other landscape side, folding, or multi-window resizing re-fits live without restarting (`configChanges`).
- **Cheat Codes (Settings → CODES):** A CODES row in Settings opens a native Android text dialog. `GIMMEMONEY` grants **999 trillion coins** ($999,000,000,000,000). Coins are 64-bit on Android (`coin_t`), and the save blob is written in a host-only **V7** layout (24-laser mask; legacy V5/V6 saves migrate automatically). Big balances print shortened (e.g. `999T`, `1.5B`).
- **Erase All Data (Settings → ERASE DATA):** Confirms with a system dialog, then wipes coins, unlocks, upgrades, high score, and settings back to a fresh install.
- **24 Laser Crystals:** The hangar LASERS tab has 24 crystals that get strictly stronger and more expensive as you go up. The hangar preview chamber actually fires the selected bolt (clipped, looping).
- **Controller Support:** Xbox / generic Bluetooth gamepads work on Android. Left stick or D-pad to move, **A / RT** to fire, **B / X / LB** to charge the beam, **Start** to pause. The on-screen stick hides while a pad is in use.
- **Boss Drone:** The wave boss is the unused mini-drone hull, scaled and recoloured (gold full boss / cyan mini-boss) so it cannot be mistaken for a hunter.
- **Life-Only Powerups:** Shield and Rapid Fire powerup drops are removed on Android; every powerup roll resolves to the **life (repair)** pickup at its original rarity (20% of rolls → the same effective drop rate as before).
- **90 Hz High Refresh Rate:** Configured for 90 Hz display refresh mode with synchronized 90 FPS Choreographer frame loop and 90 Hz time-accumulated physics.
- **Synchronized Audio Mixer:** Mixes 202 samples @ 90 Hz (18.157 kHz source) and resamples to 44.1 kHz so Android does not pitch-shift or boom the soundtrack. Menu BGM is ducked so it is not louder/faster than the in-game track.
- **Native Menu Touch:** Menus, hangar, upgrades, pause, and game-over are tapped directly. Shop tabs (`PAINTS` / `TRAILS` / `WEAPONS` / `LASERS`) use full-width touch zones, item/buy taps, and a horizontal swipe to change category. The virtual stick is hidden outside gameplay.
- **Game Modes (Android only):** Play opens a mode select. **Waves** is the classic clear-the-wave run. **Endless** never stops — threat ramps, hunters spawn at random, no wave banners. **Overdrive** is a 90-second score rush with denser random spawns.
- **Circular Virtual Stick:** In-game only floating circular analog pad (left) plus circular FIRE / BEAM (hold for big laser) buttons and a PAUSE chip.
- **Haptics:** Settings menu toggles vibrator feedback (hits, kills, beam charge, beam fire, and button presses). Uses `VibratorManager` plus `performHapticFeedback` so it actually buzzes on modern phones. Persists in the save file. Gyro / tilt steering has been removed.
- **Dynamic Phone Scaling:** The native width adapts to the current window (see above) and relayouts on rotate / multi-window resize, so there are never bars at the sides.
- **Persistent Storage:** Coins, owned loot (paints, trails, weapons, lasers), upgrades, and high score write to `files/saves/save.sav` under the app's private internal storage (`Context.getFilesDir()`). That directory is always readable and writable by the app with **no storage permission and no folder picker** at launch. Legacy `space_unlimited.sav` files are migrated automatically.

## Build

```bash
cd android
./gradlew assembleDebug
```

APK: `android/app/build/outputs/apk/debug/app-debug.apk`

Requires Android SDK + NDK (CMake). The GBA ROM build (`npm run build`) is unchanged and still uses libtonc (240×160 @ 60 FPS).
