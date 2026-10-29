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
set -e
DAEMON_BIN="/usr/bin/flydigi-bs2pro-linux-daemon"
DAEMON_SOCK="${FLYDIGI_SOCKET:-/run/user/$UID/flydigi-bs2pro-linux.sock}"

# Start daemon if not running
if ! pgrep -x "flydigi-bs2pro-linux-daemon" >/dev/null 2>&1; then
    "$DAEMON_BIN" &
    sleep 1
fi

# Launch GUI
exec /usr/bin/flydigi-bs2pro-linux --socket "$DAEMON_SOCK"
EOF
chmod +x "$appdir/usr/bin/flydigi-bs2pro-linux-wrapper"

linuxdeploy \
  --appdir "$appdir" \
  --desktop-file "$root_dir/packaging/flydigi-bs2pro-linux.desktop" \
  --icon-file "$root_dir/packaging/flydigi-bs2pro-linux.svg" \
  --plugin qt \
  --output appimage \
  --executable "$appdir/usr/bin/flydigi-bs2pro-linux-wrapper"

mkdir -p "$out_dir"
# linuxdeploy writes the final file in the current directory; normalize its name.
for image in ./*.AppImage; do
  [ -e "$image" ] || continue
  mv "$image" "$out_dir/flydigi-bs2pro-linux-${version}-x86_64.AppImage"
  echo "created $out_dir/flydigi-bs2pro-linux-${version}-x86_64.AppImage"
  exit 0
done

echo "error: linuxdeploy did not produce an AppImage" >&2
exit 1