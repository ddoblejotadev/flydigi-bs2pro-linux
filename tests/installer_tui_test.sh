#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
installer="$root_dir/installer/flydigi-installer"
test_dir="$(mktemp -d)"
trap 'rm -rf "$test_dir"' EXIT

release_dir="$test_dir/release"
install_root="$test_dir/install"
mkdir -p "$release_dir"
printf 'first image\n' > "$release_dir/flydigi-bs2pro-linux-0.1.17-x86_64.AppImage"
printf 'second image\n' > "$release_dir/flydigi-bs2pro-linux-0.1.18-x86_64.AppImage"
sha256sum "$release_dir/flydigi-bs2pro-linux-0.1.18-x86_64.AppImage" > "$release_dir/SHA256SUMS"
hash="$(cut -d' ' -f1 "$release_dir/SHA256SUMS")"
cat > "$release_dir/release.json" <<JSON
{"tag_name":"v0.1.18","name":"flydigi-bs2pro-linux v0.1.18","body":"Fix reconnect\nImprove startup","assets":[{"name":"flydigi-bs2pro-linux-0.1.18-x86_64.AppImage","browser_download_url":"file://$release_dir/flydigi-bs2pro-linux-0.1.18-x86_64.AppImage"},{"name":"SHA256SUMS","browser_download_url":"file://$release_dir/SHA256SUMS"}]}
JSON

export FLYDIGI_RELEASE_API="file://$release_dir/release.json"
export FLYDIGI_INSTALL_ROOT="$install_root"
export FLYDIGI_DESKTOP_FILE="$install_root/installed.desktop"

fail() { printf 'FAIL: %s\n' "$1" >&2; exit 1; }
assert_file() { [ -f "$1" ] || fail "expected file $1"; }
assert_contains() { grep -Fq -- "$2" "$1" || fail "expected '$2' in $1"; }

bash "$installer" --install --yes > "$test_dir/install.log"
assert_file "$install_root/versions/0.1.18/flydigi-bs2pro-linux-0.1.18-x86_64.AppImage"
[ "$(readlink "$install_root/current")" = "versions/0.1.18/flydigi-bs2pro-linux-0.1.18-x86_64.AppImage" ] || fail "current does not point to 0.1.18"
assert_contains "$install_root/installed.desktop" "Exec=\"$install_root/current\""

# A bad checksum must fail before changing the current version.
printf 'bad\n' > "$release_dir/SHA256SUMS"
if bash "$installer" --install --yes > "$test_dir/bad.log" 2>&1; then
    fail "bad checksum was accepted"
fi
[ "$(readlink "$install_root/current")" = "versions/0.1.18/flydigi-bs2pro-linux-0.1.18-x86_64.AppImage" ] || fail "bad checksum changed current"

# Publish a second local release to test cancellation, replacement, and rollback.
printf 'third image\n' > "$release_dir/flydigi-bs2pro-linux-0.1.19-x86_64.AppImage"
sha256sum "$release_dir/flydigi-bs2pro-linux-0.1.19-x86_64.AppImage" > "$release_dir/SHA256SUMS"
cat > "$release_dir/release.json" <<JSON
{"tag_name":"v0.1.19","name":"flydigi-bs2pro-linux v0.1.19","body":"Rollback test","assets":[{"name":"flydigi-bs2pro-linux-0.1.19-x86_64.AppImage","browser_download_url":"file://$release_dir/flydigi-bs2pro-linux-0.1.19-x86_64.AppImage"},{"name":"SHA256SUMS","browser_download_url":"file://$release_dir/SHA256SUMS"}]}
JSON

printf 'n\n' | bash "$installer" --install > "$test_dir/cancel.log"
[ "$(readlink "$install_root/current")" = "versions/0.1.18/flydigi-bs2pro-linux-0.1.18-x86_64.AppImage" ] || fail "cancel changed current"
bash "$installer" --install --yes > "$test_dir/upgrade.log"
[ "$(readlink "$install_root/current")" = "versions/0.1.19/flydigi-bs2pro-linux-0.1.19-x86_64.AppImage" ] || fail "upgrade did not change current"
bash "$installer" --rollback > "$test_dir/rollback.log"
[ "$(readlink "$install_root/current")" = "versions/0.1.18/flydigi-bs2pro-linux-0.1.18-x86_64.AppImage" ] || fail "rollback did not restore previous version"

printf 'installer TUI tests passed\n'
