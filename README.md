# Space Unlimited: Recharged 🚀

A native **Game Boy Advance (GBA)** remake of the original Scratch space shooter.
The Scratch project is preserved as `spaceshooter.sb3`; the game is a fresh, controller-ready implementation built from the ground up for authentic retro Game Boy Advance hardware.

---

## 🕹️ Game Boy Advance (GBA) Version

The GBA edition is written in native C (ARMv4T / libtonc) and compiles directly to a `.gba` ROM playable on real hardware (flashcarts, EverDrive, EZ-Flash), handheld emulators (Miyoo Mini, Anbernic, Steam Deck), standalone emulators (mGBA, VisualBoyAdvance, Delta), and via the included WebAssembly browser player.

### GBA Features

- **Native Mode 4 Graphics Engine:** 240×160 60 FPS double-buffered page-flipped renderer with zero screen tearing and a custom 256-color palette.
- **18.157 kHz DirectSound Audio Engine:** Hardware-timed signed PCM playback on both speakers for `menu.wav`, `game.wav`, and the dedicated `boss.wav` cue, plus polyphonic sound effects. Boss waves fade the normal track out, hold one second of entrance silence, fade the boss cue in, then resume gameplay music from its paused sample when the boss falls or the one-shot cue ends.
- **2× Arcade Pace:** Gameplay advances two simulation ticks per displayed frame, while audio remains on its independent hardware timer so faster action does not pitch-shift or starve the soundtrack.
- **Expanded Shop & Deep Progression Grind:**
  Earn coins by destroying asteroids, enemy fighters, completing waves, and racking up combo chains. Coins persist in SRAM and are spent in the **Upgrade Hangar** on permanent unlocks & upgrades:
  - **9 Ship Paints** (Solar Orange 800c, Ion Cyan [Starter 0c], Nova Violet 2,500c, Plasma Mint 5,500c, Pulsar Gold 14,000c, Crimson Void 30,000c, Obsidian Shadow 65,000c, Quantum Neon 120,000c, and the ultimate animated **Rainbow Prism** 1,000,000c)
  - **8 Engine Trails**, capped by the animated **Rainbow Trail** at 1,000,000c.
  - **16 strictly ordered Weapon Rigs:** Pulse Blaster [Starter], Twin Cannons, Scatter Array, Rail Trident, Quad Blaster, Plasma Lances, Quantum Five, Nova Star, Arc Hex, Rift Battery, Comet Swarm, Solar Lances, Starquake, Void Crown, Prism Storm, and the 1,000,000,000c **Infinity Beam**. The final rig fires a full-height continuous beam for as long as Fire is held, ramping from 20% to full damage over 0.6 seconds.
  - **5 Laser Crystals:** Ion Basic [0c], Solar Gold [5,000c], Nebula Violet [50,000c], Quantum White [500,000c], and animated **Rainbow Prism** [1,000,000c].
  - Equipping the 1,000,000c rainbow paint, trail, and crystal together turns every asteroid into an animated rainbow asteroid.
  - **8 Plain Stat Upgrades (5 Levels Each):** Speed, Fire Rate, Damage, Shield, Lives, Beam, Coins, Rapid. Names are the stat — no hull/reactor flavor text.
- **Spacious & Polished Shop UI:**
  - Tabbed category navigation (`PAINTS`, `TRAILS`, `WEAPON`, `LASERS`, `TECH`) with `L` and `R` triggers or D-pad.
  - Scrolling 5-item catalog panel with status badges (`[EQ]`, `OWN`, `Lv1/3`, `MAX`, price).
  - Live animated Ship Preview Chamber with active engine flare and weapon laser test-fire.
  - Dedicated item detail card with full stats, descriptions, and manual `[A] BUY` / `[A] EQUIP` actions.
- **SRAM Save Persistence:**
  Coins, high score, unlocked items, equipped loadout, and tech upgrade levels persist to cartridge backup memory (`0x0E000000`) and automatically sync to browser `localStorage` in the web player. On Android the same blob is written to the app-private `files/saves/save.sav` folder (`Context.getFilesDir()`), which is always readable/writable without any permission prompt or startup setup.
- **Hunter Enemy Fighters:** Hunters track your horizontal position, spawn with random hull styles and random accent colors, then fire random 2–4 shot bursts straight downward using the same equipped laser appearance and laser sound as the player.
- **Balanced Arcade Gameplay:** Asteroid splitting, hunter enemy fighters, rare powerup drops (Shield, Rapid Fire, Repair), and a timed combo multiplier system (up to ×20).
- **Boss Fights (Waves mode):** Wave **5 / 15 / 25...** is the **Razorwing** and every **10th** wave is the **Goliath** — two real pixel-art hulls authored nose-up in the exact same template language and lighting conventions as the player ships (hull ramp, canopy D-glass, accent leading edges), then flipped to fly nose-down. Each has a **key mechanic** on top of its own kit:
  - **Razorwing** *(BURNING WAKE)*: a nimble swept-delta interceptor whose strafing runs leave a hanging trail of afterburner embers that wall off the sky it just crossed — chase it and you fly into the wake. Also darts between anchors with aimed snap shots, fires widening snap fans, and feints a dive that sprays sideways.
  - **Goliath** *(SEALED DECKS)*: an armored battleship that only takes **full damage while its gun decks are open** — i.e. while it is attacking. Cruising with decks sealed, armour eats two-thirds of every hit, so you punish its ring barrages, constant-speed siege beam, walking curtain walls, and crossing broadsides instead of plinking at the armour. The HUD calls the window (`DECKS OPEN — FULL DAMAGE`).

  Both enrage at 50% and 25% HP, never repeat the same attack twice in a row, and use `boss.wav`: gameplay music fades out, the entrance gets one second of silence, the boss cue fades in, and the normal track resumes at its paused position after victory. Boss HP scales with wave/difficulty rather than the equipped gun, so every weapon purchase produces a real time-to-kill improvement.
- **Three Game Modes:** Play opens a mode select on both platforms — **Waves** (with bosses), **Endless**, and **Overdrive** (90-second score rush).
- **Big Laser Mechanic (replaces Dash):** Hold **B/R/L** for **2 seconds** to charge a full-screen piercing beam that fires for 3 seconds and reaches the top of the screen. The beam cuts through every rock and hunter in its column, dealing exactly `current laser damage ÷ 10` per frame (fractional damage accumulates, so even a 1-damage starter laser chews through rocks).
- **Settings Screen:** Difficulty (**Easy / Medium / Hard**), Music & SFX volume, and Screen Shake — plus an **Android-only Haptics** (vibration feedback) toggle. Persisted in the save file. Gyro / tilt steering has been removed.
- **Combo Coins:** Combo still multiplies coins, but the curve is much softer so a long chain is not a money printer. Locking **15x** is the payday (4.5x coins plus a lump bonus).
- **Rock Physics Overhaul:** Big rocks drift slow, medium rocks keep their classic speed, and small/tiny rocks scream past even faster. Later waves spawn a lot more rocks (and keep reinforcing the field) so it gets really difficult. Small rocks only appear once — when they fall off-screen they're gone for good (no more endless respawn wrap), so waves end cleanly.

### GBA Controls

| Action | GBA Button | Keyboard (Web Player) | Xbox / USB Gamepad |
|---|---|---|---|
| Move Ship / Navigate | **D-Pad** | `WASD` or Arrow keys | Left Stick or D-Pad |
| Fire / Buy / Equip | **A** | `Space`, `Z`, or `J` | `A` or `Right Trigger` |
| Beam (hold 2s) / Back | **B** | `Shift`, `X`, or `K` | `X`, `B`, or `Right Bumper` |
| Shop Tab Switch | **L** / **R** | `Q` / `E` or `Left/Right` | `LB` / `RB` or `Left/Right` |
| Pause / Menu | **START** | `Enter` or `P` | `Start` / Menu |
| Options / Reset | **SELECT** | `Backspace` or `Tab` | `Back` / View |

---

## 📱 Android (multiplayer NDK host)

The primary Android app is the multiplayer edition. It shares the renderer, audio, save, menu, and asset sources in `gba/`, and compiles an Android-specific fork of `game.c` with two-player support using the NDK (`PLATFORM_HOST`). Epic Online Services provides Device-ID sign-in, public Quick Match lobbies, and P2P networking; the game remains playable offline when EOS credentials are not configured. No WebView, no emulator. A Kotlin `GameView` presents the 16:9 widescreen 284×160 framebuffer scaled to the phone, runs at 90 Hz, plays the soundtrack at 44.1 kHz, uses native tap targets in menus (including shop tabs), and shows a circular virtual stick only while playing.

### Co-op reliability & fairness

- **Host-authoritative co-op with self-healing sync:** the host simulates both ships from the guest's streamed input and broadcasts full-world snapshots every 3 ticks. If packets are lost (NAT warm-up, queue overflow, UI jank), the guest detects the stale stream within ~1.3 s and asks the host to resend the world — no more getting stuck on a frozen screen.
- **Your loadout travels with you:** the guest's paint, laser crystal, weapon rig, trail, and all stat upgrades are streamed to the host, including Infinity Beam state/ramp. The host's complete rainbow set is carried in every world snapshot, so rainbow asteroids render identically for the client. Packets and the lobby bucket are protocol-versioned so mismatched builds fail cleanly.
- **Both players progress:** the host's coin balance rides in every snapshot; the guest banks co-op earnings into its own save (the host's pre-join fortune is never transferred) and records its new best score at game over.
- **P2P queues sized for 90 Hz:** the EOS P2P packet queues are raised from the tiny default so the snapshot stream survives Android frame hiccups.
- **Laser SFX matches your real fire rate:** the guest plays its own laser sound at the exact cadence the host fires for it (including big-laser charge timing), and the SFX mixer never restarts a still-playing laser sample — no more machine-gun stutter.

### 🔐 Epic Online Services credentials (where the secrets live)

The EOS credentials — `productId`, `sandboxId`, `deploymentId`, `clientId`, and
`clientSecret` — are **never committed to this repository** and never appear in
any source file. They are stored in the **GitHub repository settings**:
**Settings → Secrets and variables → Actions** as `EOS_PRODUCT_ID`,
`EOS_SANDBOX_ID`, `EOS_DEPLOYMENT_ID`, `EOS_CLIENT_ID`, and
`EOS_CLIENT_SECRET`. Only repository maintainers with settings access can see
or edit those values — nobody who clones, forks, or downloads this repo can
access them, and GitHub masks them in CI logs during builds.

- The `Build & Release (GBA ROM + Multiplayer Android APK)` workflow injects
  the repo-settings secrets into the Gradle build via environment variables.
- For local development, credentials go in `android/eos.properties`, which is
  git-ignored (`.gitignore`) — never force-add it and never paste the values
  into any committed file, issue, or PR.
- The Android client uses an untrusted **User Required Peer2Peer** policy, so
  the client ID/secret inside the APK are public by design (Epic's security
  model for client-side apps) — the deployment ID and sandbox are what gate
  real access, and those stay in repo settings too.

Play opens a **mode select** on **both** Android and GBA:

- **Waves** — clear each wave of asteroids and hunters. Wave 5/15/25... is a **mini-boss**; every 10th wave is a full **boss fight**.
- **Endless** — no waves; random hunter ships and rocks keep coming and the threat keeps rising.
- **Overdrive** — 90-second score rush with denser random spawns.

The Settings screen (Main Menu → Settings) has **Haptics** (vibration on hits, kills, beam charge-up, beam fire, and button presses), a **CODES** row — tap it to open the cheat-code dialog (native Android text input) — and **ERASE DATA** to wipe the save after a confirm dialog. Known code: **`GIMMEMONEY`** tops your coin balance up to **$999,000,000,000,000** (999 trillion — the coin counter is 64-bit on Android and the save file upgrades to the V7 layout automatically). Haptics stay **Android-only** — the GBA has no vibration motor, so `platform_queue_haptic()` compiles to a no-op there. Gyro / tilt steering is gone. The big-laser button replaces DASH on the touch pad.

Android also accepts a **Bluetooth / USB gamepad** (left stick or D-pad, A/RT fire, B/X/LB beam, Start pause). Its hangar uses the same 16-rig weapon ladder and focused five-crystal laser catalog as GBA, with working live previews. Wave bosses use the mini-drone sprite in distinct gold/cyan paint so they cannot be mistaken for hunters.

### 📖 Story Mode (Android)

Play now opens **Story Mode** — a 70-level campaign that must be finished
before the rest of the game unlocks.

- **The story of Jack RK and the Chubbs.** Once upon a time in another
  universe, aliens invaded the Chubbs. The Chubbs fought the Reality King, but
  he was too strong. Following the old advice, “if you can't beat 'em, join
  'em,” they ended the fighting by befriending him. Jack Arkey — known as
  **Jack RK**, or simply Jack — was a technology expert from another planet who
  could not let the invasion go. He wanted revenge, so he built the starter
  ship, set it ready for flight, and headed for the stars.
- **The opening speech.** First launch types out Jack RK's story page by page
  over the starfield. A tap fills the current page; a second tap turns it.
  Pages never advance on their own. Tap SKIP to jump it. It only plays
  once. It runs under **Story Mode's own soundtrack**
  (`Assets/Audio/story_mode.mp3`), which keeps playing across the level map,
  Mr Chubbs' dock, and the result cards, so the campaign never sounds like the
  arcade front end.
- **70 levels across 7 themed sectors** — The Chubb System, The Rust Yards,
  The Ice Fields, The Scrapline, Ember Reach, The Cold Vault, and The Reality
  kingdom. **35 of the 70 levels are puzzles**, with five puzzle nodes in
  every sector, so the campaign changes rhythm constantly rather than saving
  all its experiments for the finale.
- **Thirty-one puzzle rules with no more than two appearances each** make the
  puzzle half a real variety pack: *Limited Ammo*, *Signal Hunt*, *Guns
  Offline*, *Target Order*, *Color Code*, *Ricochet Run*, *Clean Combo*, *Big
  or Small*, *Safe Lane*, *Orbital Lock*, *Chain Link*, *Fragile Cargo*,
  *Mirror Aim*, *Twin Lock*, *Drone Code*, *Anchor Break*, *Ghost Signal*,
  *Clockwork*, *Sieve*, *Bomb Defusal*, *Ring Maze*, *Wall Walk*, *Scissor
  Cross*, *Spiral Step*, *Zigzag Rain*, *Pacifist*, *Sweep Code*, *Armour
  Key*, *Lockstep*, *Beacon Run*, and *Last Shot*.
  - **Shot puzzles** change what counts: exact ammo budgets, reticle order,
    one coded asteroid type, paired targets, drone-only targets, size locks,
    and rows that must be swept in sequence. Wrong choices flash the scanner,
    rewind the sequence, cost a life, or spend the limited shot budget rather
    than silently awarding progress.
  - **Movement puzzles** shut the guns down and run deterministic rings,
    walls, diagonals, spirals, zigzags, and pacifist patterns. The big laser
    is grounded on every puzzle, so no free screen wipes.
  - **Field-rule puzzles** add bouncing/mirrored shots, moving safe beacons,
    a fragile cargo hold, combo chains, and timed defusal. Their live rule is
    named on the level card before launch and shown again in the HUD.
- **Six objectives:** **clear the field**, **hunt the fighters**,
  **survive the timer**, **crack the big ones** (break N large rocks),
  **clear it on the clock**, and **puzzle levels**. Eight arcade field
  modifiers still provide extra variety outside the puzzle nodes.

  The puzzle rotation is hand-authored so a player sees the same rule at most
  twice, usually once, while difficulty and target quotas rise through the
  sectors. The level banner, map card, scanner reticles, moving-lane beacon,
  and HUD all explain the active twist before it becomes dangerous.
- **A different sky over every sector.** All seven sectors retain a distinct
  backdrop: the Chubb System's cold stars, the Rust Yards' iron haze, the Ice
  Fields' blue sleet, the Scrapline's industrial glare, Ember Reach's embers,
  the near-black Cold Vault, and The Reality kingdom's folded violet space.
- **A boss every 10 levels, each a hand-pixelled ship in the fleet's own art
  style** (authored nose-up with the same hull lighting ramp, canopy glass and
  paint accents as the player ship templates, then flipped to fly nose-down),
  and each built around a **key mechanic** no other boss uses:
  - **Ironmaw** *(THE BITE)* — a forked-jaw hunter that telegraphs, lunges and
    snaps its jaws shut at your altitude. A missed bite leaves the jaws
    **clamped and straining — double damage** until it recovers; the fight is
    bait-the-bite, punish-the-clamp. Between bites it chews ragged, uneven
    cones of scrap (real debris spray, not a neat fan).
  - **Gemini** *(THE SPLIT)* — a twin-boom raider whose seam vents angled jets
    out both sides; at 50% it **splits into two mirrored hulls sharing one
    health pool**, the clone shadowing your column while the prime hunts you.
  - **Frostbite** *(ENGINE ICING)* — an ice interceptor whose cold **freezes
    your engines while you sit still**: an icing meter climbs when parked and
    throttles your thrust to ~45%, shedding only while you move. Its web
    lattice and tracking lance are built to make you want to camp — the ice is
    why you can't.
  - **Juggernaut** *(CRUSH)* — four armour plates that soak damage until
    broken, a magnet that drags you up into its rings, thrown live asteroids,
    and a floor-slam whose shock columns rise from below.
  - **Inferno** *(THE BURN)* — two counter-rotating fire whips that **never
    stop spinning** for the entire fight, plus aimed flare bolts and a
    solar-wind curtain that always leaves one way through.
  - **Aegis** *(THE SEAL)* — **completely invulnerable** behind four rotating
    turret nodes that must be shot off first; its lockdown searchlight marches
    shots across the floor in a strict scan, and once breached it vents a
    two-armed spiral you walk between.
  - **The Cube Queen** *(THE FOLD)* — the three-stage finale: only the
    **glowing cube face** takes full damage, then **four pylons** must be
    shot off (no hull bar), then a last-stand core that **folds you across
    the screen**. Axis-aligned cube shot, not another ring barrage. Jack
    RK gets his chance at revenge.

  Every story boss opens with the same staging as the arcade ones: the music
  fades to a second of silence while the hull descends, then the dedicated
  `boss.wav` cue fades in until the kill.
- **Level map.** Fly a ship between the nodes of a sector; bosses are bigger
  nodes, cleared levels are ticked, and locked ones are greyed out.
- **Mr Chubbs' Trading Post — one dock, one visit.** Mr Chubbs, a popular Chubb
  and the kingdom's travelling shopkeeper, catches up with Jack every fifth
  level. **Leaving the dock undocks him for good** until the next scheduled
  stop. He sells weapons, laser crystals, paints, stat upgrades, and spare
  lives.
- **Chubbcoin** is the story's own currency, earned per level clear and spent
  at Mr Chubbs' Trading Post. Arcade coins are never earned or spent in the
  campaign.
- **Payouts are dynamic — the level's reward is a floor, not a fee.** What you
  actually bank depends on how you flew it: **FAST** (up to +60% for finishing
  well inside the level's par time), **KILLS** (up to +50% for what you really
  destroyed, measured against the level's expected body count), **AIM** (up to
  +20% for accuracy) and **CLEAN** (+25% for not losing a life). That combat
  share is what stops a *survive* level paying full price for hiding in a
  corner: idle out the clock without shooting and you bank the floor and
  nothing else. Replays still halve the whole payout. The result card shows the
  breakdown.
- **The opening cinematic.** The first time you enter the campaign, Jack RK's
  origin story types itself out over the starfield — 14 pages, two lines each,
  one white and one blue, scored by `story_mode.mp3`. A tap fills the current
  page, a second tap turns it (pages never auto-advance), **SKIP** drops
  straight into the level map, and `g_story.intro_seen` makes sure it only
  ever plays once.
- **Enemy variation stays intact.** Hunter ships spawn with random hull styles
  and random accent colors, keeping repeated fights visually varied.
- **Lives, wrecks & the repair yard.** You get 3 story lives. Die and you retry
  the level. Run out and the ship is a **write-off**, which costs you three
  things at once: the **last two levels you cleared are re-locked** (you fly
  them again), the **Chubbcoin they paid is taken back off your balance**, and
  the ship is **grounded for 15 real minutes** while it is repaired. The repair
  clock is wall-clock, so it keeps counting down with the game closed — the map
  shows the countdown in place of LAUNCH until the ship is handed back.
- **Everything else stays locked** — Shop, Upgrades, Multiplayer, and the
  Waves/Endless/Overdrive modes — until level 70 falls.

Story progress lives in the **V11 save block** (levels cleared, Chubbcoin,
lives, unlock flags, which of Mr Chubbs' 14 docks are already spent, and the
repair-yard deadline). Older saves — including V9/V10 campaign saves — upgrade
automatically on load and keep all their progress. The campaign is verified by a headless playthrough
harness that proves every level still terminates — see `tools/story_sim/`.

Story Mode's soundtrack is the only asset shipped as an MP3, so it has its own
converter: `python3 tools/generate_story_audio.py` regenerates
`android/app/src/main/cpp/audio_story_data.c` (committed) whenever
`Assets/Audio/story_mode.mp3` changes. It needs `miniaudio` + `numpy`, which is
why it is kept out of the dependency-free `npm run generate-assets` path. Only
the Android target compiles it — the GBA ROM has no Story Mode and never links
the samples.

### Android-only gameplay differences

- **Life-only powerups:** The Shield and Rapid Fire drops are removed. The only powerup that drops is the **life (repair)** pickup, at exactly the same rarity the repair slice had before (the underlying drop chances are unchanged). The now-dead "Rapid" duration tech upgrade is hidden from the Upgrades screen (its data stays in the save).
- **Adaptive full-screen widescreen:** The framebuffer is always 160 px tall, but its width adapts to the phone (284–480 px) so the game **fills the entire screen — no side bars**, on any aspect ratio from 16:9 up to 21:9+. The app is locked to **landscape** (`sensorLandscape`), draws under the camera cutout, and re-fits live on fold/unfold or multi-window resizes. The GBA build keeps its fixed 240×160 Mode 4 screen untouched.

`cd android && ./gradlew assembleDebug` — see `android/README.md`.

---

## 🛠️ Building & Running the GBA Version

### 1. Build the GBA ROM (`SpaceUnlimited.gba`)

```bash
npm run build
```

This compiles all C sources in `gba/src/` against `libtonc` and outputs `SpaceUnlimited.gba`.

### 2. Regenerate Assets from Source

To regenerate the 8-bit signed-PCM audio and indexed graphics C arrays from the checked-in source assets:

```bash
npm run generate-assets
```

### 3. Run the Interactive Web Player / Preview

To launch the web-based mGBA player with live controller and keyboard support on port 3000:

```bash
npm start
```

Navigate to `http://localhost:3000` to play in browser or download the `.gba` ROM.

### 4. Headless ROM Smoke Test

Boots the built ROM in the mGBA core, drives the menus and a gameplay run with synthetic input, and fails if the ROM stops producing frames or the UI wedges:

```bash
node tools/smoke_gba.mjs            # exits non-zero on hang/crash
node tools/shot_gba.mjs /tmp/shots  # write PNG screenshots instead
```

---

## 📁 Repository Layout

| Path | Description |
|---|---|
| `SpaceUnlimited.gba` | Compiled Game Boy Advance ROM |
| `android/` | Primary multiplayer Android NDK host with EOS Device-ID sign-in, public lobbies, P2P co-op, and shared `gba/` sources |
| `Assets/` | Source images and WAV audio (extracted from `spaceshooter.sb3`) |
| `gba/` | GBA C source code and headers (`src/`, `include/`) |
| `tools/` | Asset converter (`generate_gba_data.py`), GBA compiler driver (`build_gba.js`), and bundler (`bundle_web.js`) |
| `web/` | Web emulator player UI, styling, and WebAssembly mGBA assets |
| `server.js` | Web server for live interactive preview and ROM downloads |
| `spaceshooter.sb3` | Preserved original Scratch 3 project file |
