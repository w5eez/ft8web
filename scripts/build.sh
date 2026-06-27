#!/bin/sh
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/.." && pwd)
JOBS=$(nproc)

echo "Building backend..."
cmake -S "$ROOT/backend" -B "$ROOT/backend/build" -DCMAKE_BUILD_TYPE=Release
cmake --build "$ROOT/backend/build" -j"$JOBS"

echo "Building frontend..."
cd "$ROOT/frontend/ft8web-ui"
[ -d node_modules ] || npm ci
NG_CLI_ANALYTICS=false npx ng build

echo "Build complete. Restart the backend to load changes: sudo systemctl restart ft8web"
