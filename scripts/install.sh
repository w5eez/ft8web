#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# One-shot ft8web installer for Debian / Raspberry Pi OS.
# Run as a normal user from ~/ft8web:  bash scripts/install.sh

set -euo pipefail

HAMLIB_REF="${HAMLIB_REF:-master}"
BUNDLE_URL="${BUNDLE_URL:-https://sourceforge.net/projects/wsjt-x-improved/files/WSJT-X_v3.1.0/Source%20code/wsjtx-3.1.0_improved_PLUS_260522.tgz/download}"
BUNDLE_SHA256="${BUNDLE_SHA256:-f159cde764ababb2c63ee8f307f18f29db0db322e152622cc5b4e2c7575fb5a0}"

ROOT="$HOME/ft8web"
JOBS="$(nproc)"
TMP=""

log() { printf '\n\033[1;36m== %s ==\033[0m\n' "$*"; }
die() { printf '\033[1;31merror:\033[0m %s\n' "$*" >&2; exit 1; }
cleanup() {
  [ -n "${SUDO_KEEPALIVE:-}" ] && kill "$SUDO_KEEPALIVE" 2>/dev/null || true
  [ -n "$TMP" ] && rm -rf "$TMP" 2>/dev/null || true
}
trap cleanup EXIT

[ "$(id -u)" -ne 0 ] || die "run as a normal user, not root"
[ -n "${HOME:-}" ] || die "HOME is not set"
command -v sudo >/dev/null || die "sudo is required"
[ -d "$ROOT/backend" ] || die "ft8web must be checked out at $ROOT"
SELF="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
[ "$SELF" = "$ROOT" ] || die "this checkout is $SELF but must be $ROOT"

export DEBIAN_FRONTEND=noninteractive
export PKG_CONFIG_PATH="/usr/local/lib/pkgconfig:${PKG_CONFIG_PATH:-}"

if [ "${SKIP_DEPS:-0}" != 1 ] || [ "${SKIP_HAMLIB:-0}" != 1 ] || [ "${SKIP_SERVICE:-0}" != 1 ] ||
   [ "${SKIP_AVAHI:-0}" != 1 ] || [ "${SKIP_GPSD:-0}" != 1 ]; then
  sudo -v || die "sudo authentication failed"
  ( while true; do sleep 50; sudo -n true 2>/dev/null; kill -0 "$$" 2>/dev/null || exit 0; done ) &
  SUDO_KEEPALIVE=$!
fi

if [ "${SKIP_DEPS:-0}" != 1 ]; then
  log "Installing apt dependencies"
  sudo apt-get update
  sudo apt-get install -y \
    build-essential cmake pkg-config git curl ca-certificates \
    libfftw3-dev libasound2-dev libasio-dev portaudio19-dev \
    gfortran libboost-all-dev \
    qtbase5-dev qtmultimedia5-dev libqt5websockets5-dev libqt5serialport5-dev libqt5sql5-sqlite qttools5-dev \
    libusb-1.0-0-dev libreadline-dev automake autoconf libtool texinfo \
    python3 python3-requests \
    trustedqsl xvfb \
    avahi-daemon gpsd gpsd-clients \
    nodejs npm
fi

if [ "${SKIP_AVAHI:-0}" != 1 ]; then
  log "Configuring avahi to advertise ft8web.local"
  conf=/etc/avahi/avahi-daemon.conf
  if grep -qE '^[[:space:]]*#?[[:space:]]*host-name[[:space:]]*=' "$conf"; then
    sudo sed -i -E 's/^[[:space:]]*#?[[:space:]]*host-name[[:space:]]*=.*/host-name=ft8web/' "$conf"
  elif grep -qE '^[[:space:]]*\[server\]' "$conf"; then
    sudo sed -i -E '/^[[:space:]]*\[server\]/a host-name=ft8web' "$conf"
  else
    printf '\n[server]\nhost-name=ft8web\n' | sudo tee -a "$conf" >/dev/null
  fi
  sudo systemctl enable --now avahi-daemon
  sudo systemctl restart avahi-daemon
fi

if [ "${SKIP_GPSD:-0}" != 1 ]; then
  log "Setting up gpsd"
  if ! grep -qE '^DEVICES="[^"]+"' /etc/default/gpsd 2>/dev/null; then
    printf 'DEVICES="/dev/ttyACM0"\nGPSD_OPTIONS="-n"\nUSBAUTO="true"\n' | sudo tee /etc/default/gpsd >/dev/null
  fi
  sudo systemctl enable --now gpsd.socket 2>/dev/null || true
  sudo systemctl restart gpsd 2>/dev/null || true
fi

if [ "${SKIP_NODE:-0}" != 1 ]; then
  log "Checking Node.js"
  node_ok() { command -v node >/dev/null &&
    node -e 'const[a,b]=process.versions.node.split(".").map(Number);process.exit(a>18||(a==18&&b>=19)?0:1)' 2>/dev/null; }
  if node_ok; then
    echo "Node $(node -v) ok"
  else
    echo "Installing Node 20 from NodeSource..."
    sudo apt-get purge -y nodejs npm 2>/dev/null || true
    curl -fsSL https://deb.nodesource.com/setup_20.x | sudo -E bash -
    sudo apt-get install -y nodejs
    node_ok || die "Node.js is still too old after NodeSource install"
  fi
fi

if [ "${SKIP_HAMLIB:-0}" != 1 ] && { [ "${FORCE_HAMLIB:-0}" = 1 ] || ! ls /usr/local/lib/libhamlib.so* >/dev/null 2>&1; }; then
  log "Building Hamlib from source"
  HAMLIB_SRC="$ROOT/vendor/hamlib-src"
  if [ -d "$HAMLIB_SRC/.git" ]; then
    cd "$HAMLIB_SRC"
    git fetch --depth 1 origin "$HAMLIB_REF"
    git checkout -B "$HAMLIB_REF" FETCH_HEAD
  else
    git clone --depth 1 --branch "$HAMLIB_REF" https://github.com/Hamlib/Hamlib.git "$HAMLIB_SRC"
    cd "$HAMLIB_SRC"
  fi
  git clean -dfx
  ./bootstrap
  ./configure --prefix=/usr/local
  make -j"$JOBS"
  sudo make install
  sudo ldconfig
  echo -n "Installed: " && rigctl --version
else
  log "Hamlib already built — skipping"
fi

WSJTX="$ROOT/vendor/wsjtx-3.1.0"
if [ "${SKIP_WSJTX:-0}" != 1 ] && { [ "${FORCE_WSJTX:-0}" = 1 ] || [ ! -x "$WSJTX/build/jt9" ]; }; then
  log "Building WSJT-X tools"
  TMP="$(mktemp -d)"
  echo "Downloading WSJT-X source..."
  curl -L --fail -o "$TMP/bundle.tgz" "$BUNDLE_URL"
  echo "$BUNDLE_SHA256  $TMP/bundle.tgz" | sha256sum -c -
  tar -xzf "$TMP/bundle.tgz" -C "$TMP"
  SRCDIR="$(find "$TMP" -maxdepth 1 -mindepth 1 -type d | head -1)"
  [ -n "$SRCDIR" ] || die "unexpected bundle layout"
  rm -rf "$WSJTX"
  mkdir -p "$ROOT/vendor"
  mv "$SRCDIR" "$WSJTX"
  cd "$WSJTX"
  if [ ! -d src/wsjtx ] && [ -f src/wsjtx.tgz ]; then tar -xzf src/wsjtx.tgz -C src/; fi
  [ -d src/wsjtx ] || die "wsjtx source not found in bundle"
  mkdir -p build && cd build
  cmake -DCMAKE_BUILD_TYPE=Release -DWSJT_GENERATE_DOCS=OFF -DWSJT_SKIP_MANPAGES=ON ../src/wsjtx
  cmake --build . -j"$JOBS" --target jt9 wsprd ft8sim ft4sim wsprsim
  sh "$ROOT/tools/ft2/build_ft2sim.sh"
  rm -rf "$TMP"; TMP=""
else
  log "WSJT-X already built — skipping"
fi

if [ "${SKIP_BUILD:-0}" != 1 ]; then
  log "Building ft8web backend"
  cmake -S "$ROOT/backend" -B "$ROOT/backend/build" -DCMAKE_BUILD_TYPE=Release
  cmake --build "$ROOT/backend/build" -j"$JOBS"
  log "Building Angular frontend"
  cd "$ROOT/frontend/ft8web-ui"
  npm ci
  NG_CLI_ANALYTICS=false npx ng build
fi

[ -f "$ROOT/config.json" ] || cp "$ROOT/config.example.json" "$ROOT/config.json"

if [ "${SKIP_SERVICE:-0}" != 1 ]; then
  log "Installing systemd service"
  [ -x "$ROOT/backend/build/ft8web" ] || die "backend binary missing — build did not complete"
  sudo tee /etc/systemd/system/ft8web.service >/dev/null <<EOF
[Unit]
Description=ft8web - web-based FT8/FT4/FT2/WSPR station
After=network.target sound.target

[Service]
Type=simple
User=$USER
Group=$USER
WorkingDirectory=$ROOT
ExecStart=$ROOT/backend/build/ft8web
Restart=always
RestartSec=3

[Install]
WantedBy=multi-user.target
EOF
  sudo systemctl daemon-reload
  sudo systemctl enable ft8web
  sudo systemctl restart ft8web

  log "Granting $USER passwordless sudo for 'systemctl poweroff' (Settings->Station->Shut Down Pi)"
  SYSCTL_BIN="$(command -v systemctl || echo /usr/bin/systemctl)"
  SUDOERS_TMP="$(mktemp)"
  printf '# ft8web: let the Shut Down Pi button power off the host\n%s ALL=(root) NOPASSWD: %s poweroff\n' "$USER" "$SYSCTL_BIN" > "$SUDOERS_TMP"
  if sudo visudo -cf "$SUDOERS_TMP" >/dev/null 2>&1; then
    sudo install -m 0440 -o root -g root "$SUDOERS_TMP" /etc/sudoers.d/ft8web
  else
    log "WARNING: sudoers validation failed; the Shut Down Pi button will not work until fixed"
  fi
  rm -f "$SUDOERS_TMP"
fi

log "ft8web install complete"
IP="$(hostname -I 2>/dev/null | awk '{print $1}')"
cat <<EOF
ft8web is running.

Open http://${IP:-<your-ip>}:8080/ or http://ft8web.local:8080/ and set your callsign, grid, and radio
in Settings — the gear icon, top right.
EOF
