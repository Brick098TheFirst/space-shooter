# Space Unlimited — Android Multiplayer Test

This is the isolated Android/Epic Online Services test edition. The normal
`android/` app remains the original single-player build.

To avoid maintaining a second copy of the C game and the 19 MB Epic SDK, this
project deliberately reuses:

- `../gba/` — complete shared C game, renderer, audio, save, and menu sources.
- `../SDK.zip` — Epic Online Services Android SDK 1.19.1.2.

The Android host, Kotlin UI, JNI bridge, and all EOS multiplayer source live in
this `multiplayer-test/` folder. It has the separate application ID
`com.brick.spaceshooter.multiplayertest`, so both editions can be installed on
one phone.

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
  untouched; only this test edition compiles the fork.
- `app/src/main/cpp/coop.c` / `coop.h` — the networking glue: input streaming,
  snapshot fragmentation/reassembly, and game-start / leave control messages.
- `app/src/main/cpp/native-lib.c` — wires `coop_tick()` into the frame loop and
  ties the session to the EOS match state.

Known test-edition simplifications: coins are awarded to the host's balance
only (the shared run's coins don't sync to the guest), and the guest's own
laser sound is played locally for feedback rather than network-synced.

## Local credential setup

In Epic Developer Portal, create a deployment and an untrusted **User Required
Peer2Peer** client policy/client. Then:

```bash
cd multiplayer-test
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
git check-ignore -v multiplayer-test/eos.properties
```

Never use `git add -f` on that file. Never use a TrustedServer client policy in
a public APK.

## Local build

```bash
cd multiplayer-test
./gradlew assembleDebug
```

APK:

```text
multiplayer-test/app/build/outputs/apk/debug/app-debug.apk
```

EOS SDK 1.19.1.2 makes this test edition Android API 26+ and 64-bit only
(`arm64-v8a`, `x86_64`). Gradle expands the SDK into an ignored build directory.

## GitHub Actions secrets

A ready-to-use workflow template is included at
`multiplayer-test/github-actions/multiplayer-test.yml`. Copy it to
`.github/workflows/multiplayer-test.yml` to activate it. The workflow reads
these encrypted repository Actions secrets:

- `EOS_PRODUCT_ID`
- `EOS_SANDBOX_ID`
- `EOS_DEPLOYMENT_ID`
- `EOS_CLIENT_ID`
- `EOS_CLIENT_SECRET`

Create each one under **GitHub repository → Settings → Secrets and variables →
Actions → Repository secrets → New repository secret**. Then open **Actions →
Multiplayer Test APK → Run workflow**. Download the APK from the completed run's
`space-unlimited-multiplayer-test` artifact.

Pull requests without secrets still compile an offline-capable APK whose menu
shows `ONLINE SETUP`; secrets are not available to pull requests from forks.
