#!/usr/bin/env bash
set -euo pipefail

version="${1:-0.1.0}"
root_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root_dir"
build_dir="${BUILD_DIR:-$root_dir/build}"
out_dir="${OUTPUT_DIR:-$root_dir/dist}"
appdir="$out_dir/AppDir"

command -v linuxdeploy >/dev/null || {
  echo "error: linuxdeploy is required to build the AppImage" >&2
  echo "download it in CI or install it locally; do not run an unverified binary" >&2
  exit 1
}
command -v appimagetool >/dev/null || {
  echo "error: appimagetool is required to build the AppImage" >&2
  echo "download it in CI or install it locally; do not run an unverified binary" >&2
  exit 1
}
command -v linuxdeploy-plugin-qt >/dev/null || {
  echo "error: linuxdeploy-plugin-qt is required to bundle Qt" >&2
  echo "download it in CI or install it locally; do not run an unverified binary" >&2
  exit 1
}

# Build the daemon
echo "Building daemon..."
cargo build --manifest-path daemon/Cargo.toml --locked --release

rm -rf "$appdir"
mkdir -p "$appdir/usr"
cmake --install "$build_dir" --prefix "$appdir/usr"

# Install the daemon binary
mkdir -p "$appdir/usr/bin"
install -m755 daemon/target/release/flydigi-bs2pro-linux-daemon "$appdir/usr/bin/flydigi-bs2pro-linux-daemon"

# Create a wrapper script that starts the daemon before the GUI
cat > "$appdir/usr/bin/flydigi-bs2pro-linux-wrapper" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
app_root="${APPDIR:-}"
if [ -n "$app_root" ]; then
    daemon_bin="$app_root/usr/bin/flydigi-bs2pro-linux-daemon"
    gui_bin="$app_root/usr/bin/flydigi-bs2pro-linux"
else
    daemon_bin="/usr/bin/flydigi-bs2pro-linux-daemon"
    gui_bin="/usr/bin/flydigi-bs2pro-linux"
fi
daemon_sock="${FLYDIGI_SOCKET:-${XDG_RUNTIME_DIR:-/run/user/$UID}/flydigi-bs2pro-linux.sock}"

# Start daemon if not running.
if ! pgrep -x "flydigi-bs2pro-linux-daemon" >/dev/null 2>&1; then
    "$daemon_bin" &
    sleep 1
fi

exec "$gui_bin" --socket "$daemon_sock"
EOF
chmod +x "$appdir/usr/bin/flydigi-bs2pro-linux-wrapper"

# linuxdeploy expects ELF executables here; the shell wrapper remains the
# desktop entrypoint but must not be passed as an executable for ELF scanning.
linuxdeploy \
  --appdir "$appdir" \
  --desktop-file "$root_dir/packaging/flydigi-bs2pro-linux.desktop" \
  --icon-file "$root_dir/packaging/flydigi-bs2pro-linux.svg" \
  --plugin qt \
  --executable "$appdir/usr/bin/flydigi-bs2pro-linux" \
  --executable "$appdir/usr/bin/flydigi-bs2pro-linux-daemon"

# Directly launching an AppImage runs AppRun, not the desktop Exec field.
# Make AppRun enter the bundled wrapper so the daemon always starts before GUI.
cat > "$appdir/AppRun" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
app_root="$(dirname "$(readlink -f "$0")")"
export APPDIR="$app_root"
exec "$app_root/usr/bin/flydigi-bs2pro-linux-wrapper" "$@"
EOF
chmod +x "$appdir/AppRun"

mkdir -p "$out_dir"
output="$out_dir/flydigi-bs2pro-linux-${version}-x86_64.AppImage"
ARCH=x86_64 appimagetool "$appdir" "$output"
echo "created $output"