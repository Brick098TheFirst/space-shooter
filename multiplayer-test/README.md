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

The lobby and P2P transport are working foundations. A host-authoritative second
ship and synchronized game entities are still the next gameplay phase; this
edition does not claim that the existing single-player C simulation is already
network synchronized.

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
