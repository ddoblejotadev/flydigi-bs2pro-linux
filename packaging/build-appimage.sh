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

rm -rf "$appdir"
mkdir -p "$appdir/usr"
cmake --install "$build_dir" --prefix "$appdir/usr"

linuxdeploy \
  --appdir "$appdir" \
  --desktop-file "$root_dir/packaging/flydigi-bs2pro-linux.desktop" \
  --icon-file "$root_dir/packaging/flydigi-bs2pro-linux.svg" \
  --output appimage \
  --executable "$appdir/usr/bin/flydigi-bs2pro-linux"

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
