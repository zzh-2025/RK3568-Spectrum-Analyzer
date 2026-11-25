#!/usr/bin/env bash
set -Eeuo pipefail
SCRIPT_DIR="$(cd "$(dirname "\${BASH_SOURCE[0]}")" && pwd)"
VERSION="$(tr -d '\r\n' < "$SCRIPT_DIR/VERSION")"
ARCH="$(uname -m)"
INSTALL_ROOT="\${SPECTRUM_INSTALL_ROOT:-$HOME/.local/opt/rk3568-spectrum-analyzer}"
RELEASES="$INSTALL_ROOT/releases"
DEST="$RELEASES/$VERSION"
if [[ "$ARCH" != "aarch64" && "$ARCH" != "arm64" ]]; then echo "ERROR: ARM64 required (detected: $ARCH)." >&2; exit 1; fi
for name in spectrum_qt spectrum_batch coupling_batch; do [[ -x "$SCRIPT_DIR/bin/$name" ]] || { echo "ERROR: missing executable: $SCRIPT_DIR/bin/$name" >&2; exit 1; }; done
mkdir -p "$RELEASES"
if [[ -e "$DEST" ]]; then echo "ERROR: version $VERSION already installed at $DEST; use a new version for upgrades." >&2; exit 1; fi
TMP="$RELEASES/.\${VERSION}.installing.$$"
LINK_TMP="$INSTALL_ROOT/.current.new.$$"
cleanup() { rm -rf "$TMP"; rm -f "$LINK_TMP"; }
trap cleanup EXIT
mkdir "$TMP"
cp -a "$SCRIPT_DIR/." "$TMP/"
chmod 755 "$TMP/bin/spectrum_qt" "$TMP/bin/spectrum_batch" "$TMP/bin/coupling_batch"
mv "$TMP" "$DEST"
ln -s "releases/$VERSION" "$LINK_TMP"
mv -Tf "$LINK_TMP" "$INSTALL_ROOT/current"
echo "Installed version: $VERSION"
echo "Active release: $INSTALL_ROOT/current"
echo "Persistent user data was not modified."
echo "Launch with: $INSTALL_ROOT/current/launch.sh"
