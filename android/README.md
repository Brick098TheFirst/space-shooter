# Space Unlimited: Recharged — Android Native Kotlin Edition 🚀📱

An authentic widescreen retro space shooter built natively in **Kotlin** for **Android** mobile devices, featuring high-refresh-rate 90Hz/120Hz gameplay, virtual analog joystick, touch navigation, full shop upgrades, and true fullscreen immersive mode with zero top or bottom system bars.

---

## 🌟 Highlights & Features

- **📱 True Native Android Architecture (Pure Kotlin):**
  - **Zero Web Player / Zero HTML / Zero WebView:** Compiled natively to ART / JVM bytecode using modern Kotlin (`com.brick.spaceshooter`).
  - **Direct Android SurfaceView & Canvas Renderer:** Pixel-perfect low-res rendering (`384 × 216` 16:9 widescreen canvas with bitmap filtering and anti-aliasing disabled for crisp, retro square pixels identical to the GBA screen).
  - **No GBA Files in Folder:** Entirely self-contained Android app module with native Android resources (`assets/images`, `res/raw`, `res/values`).

- **🎮 Mobile Touchscreen Controls & Virtual Analog Joystick:**
  - **Virtual Analog Joystick:** Touch anywhere on the left half of the screen to steer in 360 degrees smoothly with dynamic deadzone and tactile knob feedback (or select Fixed Joystick mode in Controls).
  - **Tactile Action Buttons:**
    - **FIRE Button:** Ergonomic right-thumb button supporting tap-to-fire and hold-for-autofire.
    - **DASH Button:** Afterburner evasion burst with invulnerability window, engine trail fountain, and recharge cooldown gauge.
    - **PAUSE Button:** Top-right instant pause button.
  - **Direct Touch Navigation:** Tap menu buttons, hangar tabs (`PAINTS`, `TRAILS`, `WEAPONS`, `LASERS`), upgrade cards, and item cards directly on the touchscreen.
  - **Haptic Feedback:** Tactile vibration on firing, dashing, explosions, getting hit, and shop purchases using Android `Vibrator` / `VibratorManager`.

- **⚡ 120Hz & 90Hz High-Refresh-Rate Variable Loop:**
  - Configures window display attributes and frame rates to request the display's highest available refresh rate (e.g. 90Hz on 90Hz screens, 120Hz on 120Hz screens).
  - Uses a deterministic **120Hz fixed-timestep physics accumulator** (`8.333 ms` per physics tick) so game speed is 100% consistent across 60 FPS, 90 FPS, 120 FPS, or 144 FPS displays.
  - Real-time in-game FPS and refresh rate counter on the HUD.

- **📺 Widescreen Retro Pixel Art:**
  - All entity sizes match the GBA version 1-1: Ship (20×16), Hunter Drones (20×16), Large Asteroids (24×24), Medium Asteroids (16×16), Small Asteroids (10×10), Tiny Asteroids (6×6), Lasers (4×10 / 6×14), Shield (24×24), Explosions (24×24).
  - Immersive Fullscreen Mode: Configures `WindowCompat.setDecorFitsSystemWindows(window, false)` and hides system status bars and bottom navigation bars.
  - Boots directly into the Main Menu (`Screen.MAIN_MENU`) with background music playing immediately upon launch.

- **🛡️ Full Upgrade Hangar & Shop:**
  - **9 Ship Paints:** Solar Orange, Ion Cyan (Starter), Nova Violet, Plasma Mint, Pulsar Gold, Crimson Void, Obsidian Dark, Quantum Neon, and the animated **Rainbow Prism** with dynamic chromatic wave across wings and fuselage!
  - **8 Engine Trails:** Ember Fire, Ion Cyan (Starter), Nova Purple, Aurora Mint, Solar Gold, Crimson Flame, Void Shadow, and the animated **Rainbow Trail**!
  - **8 Weapon Rigs:** Single Blaster (Starter), Twin Cannons, Spread Cannon, Focused Beam, Triple Blaster, Plasma Flak, Quantum Core, Nova Annihilator (God tier).
  - **12 Laser Crystals:** Ion Basic, Solar Gold, Nebula Violet, Toxic Mint, Crimson Fury, Emerald Surge, Void Shadow, Rainbow Laser (animated spectrum bolts), Inferno Red, Frost Blue, Photon Gold, Omega Prism (God tier).
  - **8 Tech Tree Upgrades (5 Levels Each):**
    - **Ion Engine:** Ship speed from 0.70x to 2.00x.
    - **Fire Rate:** Firing speed from 2 shots/sec to 10+ shots/sec.
    - **Plasma Core:** +1..+5 damage across all weapons.
    - **Shield Battery:** Starting and maximum shield capacity (up to 6 shields).
    - **Hull Plating:** Extra starting lives (up to 7 lives).
    - **Afterburner:** Cuts Dash cooldown down to 0.40s with extended invulnerability.
    - **Graviton Magnet:** Boosts coin drops (+175% at max) with magnetic pull on pickups.
    - **Overdrive Unit:** Extends Rapid Fire powerup duration up to 26 seconds.
  - **Live Ship Preview Chamber:** Displays active ship paint, animated engine exhaust plume, and live test-firing weapon bolts.

- **🎵 Authentic Audio Engine:**
  - Uses the exact same source WAV audio files (`menu.wav`, `game.wav`, `laser.wav`, `explosion.wav`, `pickup.wav`) stored in `res/raw`.
  - Android `SoundPool` for zero-latency polyphonic sound effects and `MediaPlayer` for seamless background music loops.
  - Save Persistence: All settings, coins, high score, unlocked items, and upgrade levels persist automatically via Android `SharedPreferences`.

---

## 🛠️ Building & Running the Android App

### Build Debug APK via Gradle

From the `android/` directory:

```bash
chmod +x gradlew
./gradlew assembleDebug
```

The compiled APK will be output to:
`android/app/build/outputs/apk/debug/app-debug.apk`
