#!/usr/bin/env bash
# Stage the HTML5 Android edition and create a Capacitor native project.
#
# The checked-in android/ folder is the mobile web game, not a Capacitor
# native project. `npx cap add android` always writes to ./android, so this
# script copies the game into www/ first, then generates the native project.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

if [[ ! -f android/index.html ]]; then
  echo "error: expected the HTML5 game at android/index.html" >&2
  exit 1
fi

# Pin Capacitor 6 so JDK 17 runners stay compatible.
npm install @capacitor/core@6 @capacitor/cli@6 @capacitor/android@6

rm -rf www
mkdir -p www
cp -a android/. www/

# Free ./android for Capacitor's native project.
rm -rf android
rm -f capacitor.config.json capacitor.config.ts

npx cap init "Space Unlimited" "com.brick.spaceshooter" --web-dir=www
npx cap add android
npx cap sync android

echo "Capacitor Android project is ready in ./android"
