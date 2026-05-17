#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
DISTRO="${MIRAGE_PROOT_DISTRO:-ubuntu}"
BUILD_DIR="$ROOT_DIR/termux/build"
RUNTIME_JSON="$BUILD_DIR/openxr-shim/openxr_mirage_runtime.json"

if ! command -v proot-distro >/dev/null 2>&1; then
  echo "proot-distro is required. Install it in Termux with: pkg install proot-distro"
  exit 1
fi

if ! proot-distro list | grep -Eq "(^|[[:space:]])${DISTRO}($|[[:space:]])"; then
  echo "proot distro '$DISTRO' is not installed. Install it with: proot-distro install $DISTRO"
  exit 1
fi

read -r -d '' DEFAULT_COMMAND <<EOF || true
set -e
cd "$ROOT_DIR"
cmake -S termux -B termux/build -DCMAKE_BUILD_TYPE=Release
cmake --build termux/build -j
export XR_RUNTIME_JSON="$RUNTIME_JSON"
echo "XR_RUNTIME_JSON=\$XR_RUNTIME_JSON"
echo "Starting miragebridge-daemon on @miragebridge.termux"
termux/build/miragebridge-daemon/miragebridge-daemon &
DAEMON_PID=\$!
trap 'kill \$DAEMON_PID 2>/dev/null || true' EXIT
if [ "\$#" -gt 0 ]; then
  exec "\$@"
fi
exec bash
EOF

proot-distro login "$DISTRO" --shared-tmp -- bash -lc "$DEFAULT_COMMAND" -- "$@"
