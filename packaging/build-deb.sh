#!/usr/bin/env bash
set -euo pipefail

version="${1:-0.1.0}"
root_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root_dir"
build_dir="${BUILD_DIR:-$root_dir/build}"
out_dir="${OUTPUT_DIR:-$root_dir/dist}"
package_dir="$out_dir/flydigi-bs2pro-linux_${version}_amd64"

# Keep dist/ as a single-release staging area. Installed files are managed by dpkg;
# these are only generated artifacts and may be safely replaced on each build.
mkdir -p "$out_dir"
find "$out_dir" -maxdepth 1 -type f -name 'flydigi-bs2pro-linux_*.deb' -delete
find "$out_dir" -mindepth 1 -maxdepth 1 -type d -name 'flydigi-bs2pro-linux_*_amd64' -exec rm -rf -- {} +

command -v dpkg-deb >/dev/null || {
  echo "error: dpkg-deb is required (run this on Debian/Ubuntu or install dpkg tools)" >&2
  exit 1
}

# Build the daemon
echo "Building daemon..."
cargo build --manifest-path daemon/Cargo.toml --locked --release

cmake --install "$build_dir" --prefix "$package_dir/usr"

# Install the daemon binary
mkdir -p "$package_dir/usr/bin"
install -m755 "$root_dir/daemon/target/release/flydigi-bs2pro-linux-daemon" "$package_dir/usr/bin/flydigi-bs2pro-linux-daemon"

# Grant the active desktop user session-scoped access to supported hidraw devices.
install -Dm644 "$root_dir/packaging/70-flydigi-bs2pro-linux.rules" \
  "$package_dir/lib/udev/rules.d/70-flydigi-bs2pro-linux.rules"

# Create a systemd user service for the daemon
mkdir -p "$package_dir/usr/lib/systemd/user"
cat > "$package_dir/usr/lib/systemd/user/flydigi-bs2pro-linux-daemon.service" <<'EOF'
[Unit]
Description=Flydigi BS2 Pro Linux Daemon
After=bluetooth.target

[Service]
Type=simple
Environment=FLYDIGI_SOCKET=%t/flydigi-bs2pro-linux.sock
ExecStart=/usr/bin/flydigi-bs2pro-linux-daemon
Restart=on-failure
RestartSec=5

[Install]
WantedBy=default.target
EOF

# Create a wrapper script that starts the daemon via systemd
cat > "$package_dir/usr/bin/flydigi-bs2pro-linux-wrapper" <<'EOF'
#!/usr/bin/env bash
set -e
DAEMON_SOCK="${FLYDIGI_SOCKET:-${XDG_RUNTIME_DIR:-/run/user/$UID}/flydigi-bs2pro-linux.sock}"

# Ensure daemon is running
systemctl --user is-active flydigi-bs2pro-linux-daemon >/dev/null 2>&1 || systemctl --user start flydigi-bs2pro-linux-daemon

# Launch GUI
exec /usr/bin/flydigi-bs2pro-linux --socket "$DAEMON_SOCK"
EOF
chmod +x "$package_dir/usr/bin/flydigi-bs2pro-linux-wrapper"

install -Dm644 "$root_dir/packaging/flydigi-bs2pro-linux.desktop" \
  "$package_dir/usr/share/applications/flydigi-bs2pro-linux.desktop"
install -Dm644 "$root_dir/packaging/flydigi-bs2pro-linux.svg" \
  "$package_dir/usr/share/icons/hicolor/scalable/apps/flydigi-bs2pro-linux.svg"

# Update desktop file to use wrapper
sed -i 's|Exec=flydigi-bs2pro-linux|Exec=flydigi-bs2pro-linux-wrapper|' "$package_dir/usr/share/applications/flydigi-bs2pro-linux.desktop"

install -Dm644 /dev/stdin "$package_dir/DEBIAN/control" <<EOF
Package: flydigi-bs2pro-linux
Version: $version
Section: utils
Priority: optional
Architecture: amd64
Maintainer: flydigi-bs2pro-linux contributors
Description: Flydigi BS2 Pro Linux control panel
 Native Qt/QML control panel for the Flydigi BS2 Pro cooler with built-in daemon.
 No external daemon dependency required.
EOF

dpkg-deb --build --root-owner-group "$package_dir" "$out_dir/flydigi-bs2pro-linux_${version}_amd64.deb"
echo "created $out_dir/flydigi-bs2pro-linux_${version}_amd64.deb"