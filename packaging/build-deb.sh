#!/usr/bin/env bash
set -euo pipefail

version="${1:-0.1.0}"
root_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${BUILD_DIR:-$root_dir/build}"
out_dir="${OUTPUT_DIR:-$root_dir/dist}"
package_dir="$out_dir/flydigi-bs2pro-linux_${version}_amd64"

command -v dpkg-deb >/dev/null || {
  echo "error: dpkg-deb is required (run this on Debian/Ubuntu or install dpkg tools)" >&2
  exit 1
}

cmake --install "$build_dir" --prefix "$package_dir/usr"
install -Dm644 "$root_dir/packaging/flydigi-bs2pro-linux.desktop" \
  "$package_dir/usr/share/applications/flydigi-bs2pro-linux.desktop"
install -Dm644 "$root_dir/packaging/flydigi-bs2pro-linux.svg" \
  "$package_dir/usr/share/icons/hicolor/scalable/apps/flydigi-bs2pro-linux.svg"
install -Dm644 /dev/stdin "$package_dir/DEBIAN/control" <<EOF
Package: flydigi-bs2pro-linux
Version: $version
Section: utils
Priority: optional
Architecture: amd64
Maintainer: flydigi-bs2pro-linux contributors
Description: Flydigi BS2 Pro Linux control panel
 Native Qt/QML control panel for the Flydigi BS2 Pro cooler.
 The flydigictld daemon is a separate dependency and is not installed by this package.
EOF

mkdir -p "$out_dir"
dpkg-deb --build --root-owner-group "$package_dir" "$out_dir/flydigi-bs2pro-linux_${version}_amd64.deb"
echo "created $out_dir/flydigi-bs2pro-linux_${version}_amd64.deb"
