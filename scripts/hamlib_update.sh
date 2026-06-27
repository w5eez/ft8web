#!/usr/bin/env bash
# Pull the latest Hamlib (master by default), rebuild + reinstall it, then
# rebuild the ft8web backend against it and restart the service.
#   HAMLIB_REF=<branch|tag>  override the ref (default: master)
set -euo pipefail

ROOT="$HOME/ft8web"
JOBS="$(nproc)"
REF="${HAMLIB_REF:-master}"
SRC="$ROOT/vendor/hamlib-src"

log() { printf '\n\033[1;36m== %s ==\033[0m\n' "$*"; }

[ "$(id -u)" -ne 0 ] || { echo "run as a normal user, not root" >&2; exit 1; }
sudo -v

log "Fetching Hamlib ($REF)"
if [ -d "$SRC/.git" ]; then
  cd "$SRC"
  git fetch --depth 1 origin "$REF"
  git checkout -B "$REF" FETCH_HEAD
else
  git clone --depth 1 --branch "$REF" https://github.com/Hamlib/Hamlib.git "$SRC"
  cd "$SRC"
fi
git clean -dfx

log "Building Hamlib"
./bootstrap
./configure --prefix=/usr/local
make -j"$JOBS"
sudo make install
sudo ldconfig
echo -n "Installed: " && rigctl --version

log "Rebuilding ft8web backend"
cmake --build "$ROOT/backend/build" --clean-first -j"$JOBS"

log "Restarting ft8web"
sudo systemctl restart ft8web
sleep 2
systemctl is-active ft8web

log "Done"
