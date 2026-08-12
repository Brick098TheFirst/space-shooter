# Space Unlimited: Recharged — Android Edition 🚀📱

An authentic widescreen retro space shooter built specifically for **Android** mobile devices, featuring high-refresh-rate 90Hz/120Hz gameplay, virtual analog joystick, touch navigation, full shop upgrades, and true fullscreen immersive mode with zero top or bottom system bars.

---

## 🌟 Highlights & Features

- **🎮 Mobile Touchscreen Controls & Virtual Analog Joystick:**
  - **Virtual Analog Joystick:** Touch anywhere on the left half of the screen to steer in 360 degrees smoothly with dynamic deadzone and tactile knob feedback (or select Fixed Joystick mode in Settings).
  - **Tactile Action Buttons:**
    - **FIRE Button:** Ergonomic right-thumb button supporting tap-to-fire and hold-for-autofire.
    - **DASH Button:** Afterburner evasion burst with invulnerability window, engine trail fountain, and real-time circular recharge cooldown gauge.
  - **Direct Touch Navigation:** Tap tabs (`PAINTS`, `TRAILS`, `WEAPONS`, `LASERS`, `TECH`), swipe between categories, and tap item cards directly.
  - **Haptic Feedback:** Tactile vibration on firing, dashing, explosions, getting hit, and shop purchases.

- **⚡ 120Hz & 90Hz High-Refresh-Rate Variable Loop:**
  - Uses a deterministic **120Hz fixed-timestep physics accumulator** with frame interpolation.
  - Runs at full native frame rates (60 FPS, 90 FPS on 90Hz displays, 120 FPS on 120Hz displays, or 144 FPS) without speeding up or slowing down game physics!
  - Real-time in-game FPS and refresh rate counter on the HUD.

- **📺 Widescreen Retro Pixel Art:**
  - Pixel-perfect low-res rendering (`384 × 216` 16:9 widescreen canvas with `image-rendering: pixelated`).
  - **All entity sizes match the GBA version:** Ship (20×16), Hunter Drones (20×16), Large Asteroids (24×24), Medium Asteroids (16×16), Small Asteroids (10×10), Tiny Asteroids (6×6), Lasers (4×10 / 6×14), Shield (24×24), Explosions (24×24).
  - Widescreen aspect ratio extends tactical peripheral radar for asteroids and hostiles while leaving outer zones clear for thumbs.

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
  - **Live Animated Ship Preview Chamber:** Displays active ship paint, animated engine exhaust plume, and live test-firing weapon bolts.

- **🎵 Authentic Audio Engine:**
  - Exact source WAV audio (`menu.wav`, `game.wav`, `laser.wav`, `explosion.wav`, `pickup.wav`).
  - Web Audio API polyphonic mixer with volume controls and mobile audio unlock.

- **💾 Save Persistence:**
  - Automatically saves coins, high score, owned items bitmask, equipped loadout, tech upgrade levels, and settings to `localStorage`.

- **📱 Tuff Android Fullscreen Immersive Mode:**
  - Hides both the top status bar and bottom Android navigation bar / gesture pill (`requestFullscreen({ navigationUI: "hide" })`).
  - Screen Orientation lock (`landscape`) and Screen Wake Lock to prevent screen dimming during gameplay.
  - Safe-area insets (`env(safe-area-inset-*)`) protect against camera punch-holes and notches.

---

## 🚀 Running the Android Edition

### 1. Web Preview & Local Play

Start the included Node.js web server:

```bash
npm start
```

Open your mobile browser (or Chrome / Brave / Firefox with mobile devtools) and navigate to:
```
http://localhost:3000/android/
```

### 2. Install as PWA on Android Phone

1. Open `http://<your-ip>:3000/android/` in **Google Chrome** on your Android phone.
2. Tap the three dots menu (**⋮**) in Chrome.
3. Select **"Install app"** or **"Add to Home screen"**.
4. Launch from your home screen for full standalone immersive mode with zero browser address bar or system navigation bar!

### 3. Packaging as Native APK (Capacitor / Cordova / Bubblewrap)

To build a standalone `.apk` for Google Play or sideloading:

```bash
# Using Bubblewrap (Google TWA CLI):
npx @bubblewrap/cli init --manifest=http://localhost:3000/android/manifest.json
npx @bubblewrap/cli build

# Or using Capacitor:
npm install @capacitor/core @capacitor/cli @capacitor/android
npx cap init "Space Unlimited" "com.brick.spaceshooter" --web-dir=android
npx cap add android
npx cap open android
```

---

## 🕹️ Controls Summary

| Action | Touchscreen (Android) | Keyboard Fallback | Gamepad |
|---|---|---|---|
| **Steer Ship / Move** | Left Thumb Virtual Joystick | `WASD` or Arrow Keys | Left Stick / D-Pad |
| **Fire Blaster / Buy** | Right Thumb `FIRE` Button (tap or hold) | `Space`, `Z`, or `J` | `A` or `Right Trigger` |
| **Dash Evade / Back** | Right Thumb `DASH` Button | `Shift`, `X`, or `K` | `X`, `B`, or `Right Bumper` |
| **Pause Mission** | Top-Right `⏸` Button | `P` or `Escape` | `Start` / Menu |
| **Shop Tabs** | Tap tabs directly or swipe | `Q` / `E` or `Left/Right` | `LB` / `RB` |
