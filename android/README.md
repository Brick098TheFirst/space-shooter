# Space Unlimited — Android Multiplayer

This is the primary Android edition of Space Unlimited. It includes Epic Online
Services multiplayer and replaces the former single-player-only Android app.
The game still works offline when EOS credentials are not configured.

To avoid duplicating most of the C game and the Epic SDK, this project reuses:

- `../gba/` — shared renderer, audio, save, menu, assets, and platform sources.
- `../SDK.zip` — Epic Online Services Android SDK 1.19.1.2.

The Android host, Kotlin UI, JNI bridge, forked gameplay source, and all EOS
multiplayer code live in this `android/` folder. It uses the primary application
ID `com.brick.spaceshooter`.

## Implemented multiplayer foundation

- EOS SDK initialization and Android foreground/background lifecycle handling.
- Automatic EOS Connect Device-ID sign-in.
- Public two-player Quick Match lobby search/create/join.
- Empty-host retry to resolve simultaneous matchmaking races.
- EOS P2P connection acceptance and generic packet send/receive JNI bridge.
- Main-menu status chip for setup, login, search, waiting, match, and errors.
- Credentials supplied only by an ignored local file or environment variables.

## Two-player synchronized co-op

When a Quick Match connects, the game enters **host-authoritative co-op**:

- The **host** runs the full simulation — both ships, the asteroids, drones,
  boss, bullets and powerups are all simulated in one shared world on the host.
  The host broadcasts a compact snapshot of the whole world to the guest.
- The **guest** stops simulating entirely. It streams its input (movement,
  fire, beam) plus its equipped loadout to the host every frame, and simply
  renders the host's snapshots — so both players see the *exact same* asteroids,
  enemies and boss, and the same shared score/wave.
- Each ship keeps its **own paint, laser crystal, weapon rig and engine trail**.
  The host simulates the guest ship with the guest's own fire-rate / damage /
  speed numbers, and bullets render in each owner's laser colour.
- Snapshots are larger than one EOS packet (1170 bytes), so the host fragments
  each one over reliable-ordered packets and the guest reassembles before
  applying. The guest extrapolates gently between snapshots for smooth motion.
- The host starts a normal game (mode select → play) and the guest is dragged
  into the same run automatically. If the host restarts from game-over, the
  guest follows. Leaving the run / leaving the lobby ends the co-op session.

### Co-op source layout (this folder)

- `app/src/main/cpp/game.c` / `game.h` — a **forked copy** of the shared GBA
  game with the second-player support: a `player2` in `GameState`, per-owner
  bullets, per-owner laser rendering, a host-side second-ship simulation, and
  the snapshot serialize/apply/render hooks. The unmodified `gba/` sources are
  untouched; only the Android edition compiles the fork.
- `app/src/main/cpp/coop.c` / `coop.h` — the networking glue: input streaming,
  snapshot fragmentation/reassembly, and game-start / leave control messages.
- `app/src/main/cpp/native-lib.c` — wires `coop_tick()` into the frame loop and
  ties the session to the EOS match state.

Co-op session model: the host is authoritative and simulates both ships;
the guest streams input, renders host snapshots, and banks its share of the
run's coins and best score into its own save. The guest's laser sound is
played locally, matched to the host's exact fire cadence, rather than
network-synced (SFX samples are never retriggered while still playing).

## Local credential setup

> 🔐 For the released APK, credentials come from the **GitHub repository
> settings** (Settings → Secrets and variables → Actions): `EOS_PRODUCT_ID`,
> `EOS_SANDBOX_ID`, `EOS_DEPLOYMENT_ID`, `EOS_CLIENT_ID`,
> `EOS_CLIENT_SECRET`. They are never stored in the repository itself, so
> nobody who clones or forks it can access them — GitHub masks them in CI
> logs and only maintainers with settings access can view or edit them.

In Epic Developer Portal, create a deployment and an untrusted **User Required
Peer2Peer** client policy/client. Then:

```bash
cd android
cp eos.properties.example eos.properties
```

Fill in the ignored `eos.properties`:

```properties
productId=
sandboxId=
deploymentId=
clientId=
clientSecret=
```

Confirm Git will ignore it:

```bash
git check-ignore -v android/eos.properties
```

Never use `git add -f` on that file. Never use a TrustedServer client policy in
a public APK.

## Local build

```bash
cd android
./gradlew assembleDebug
```

APK:

```text
android/app/build/outputs/apk/debug/app-debug.apk
```

### APK versioning and update signature

GitHub Actions passes a unique `ANDROID_VERSION_CODE` and
`ANDROID_VERSION_NAME` into every build (`1.0.<workflow run number>`). The
version values are determined before Gradle runs, so they are embedded in the
APK rather than existing only in the GitHub release title.

Downloadable APKs are debug builds, so they use the intentionally public,
repository-owned `app/space-unlimited-debug.p12` signing identity. **Do not
replace or regenerate that file:** Android only permits an installed app to be
updated by another APK signed with the same key. This is a distribution key for
test/debug APKs only, not a private Play Store release key.

EOS SDK 1.19.1.2 makes the Android app API 26+ and 64-bit only
(`arm64-v8a`, `x86_64`). Gradle expands the SDK into an ignored build directory.
