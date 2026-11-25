#!/usr/bin/env bash
set -Eeuo pipefail
ROOT="$(cd "$(dirname "\${BASH_SOURCE[0]}")/.." && pwd)"
VERSION="$(tr -d '\r\n' < "$ROOT/VERSION")"
ARCH="$(uname -m)"
BUILD_JOBS="\${BUILD_JOBS:-2}"
if [[ ! "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+([.-][A-Za-z0-9.-]+)?$ ]]; then echo "ERROR: invalid VERSION: $VERSION" >&2; exit 1; fi
if [[ "$ARCH" != "aarch64" && "$ARCH" != "arm64" ]]; then echo "ERROR: build on RK3568 ARM64 (detected: $ARCH)." >&2; exit 1; fi
for tool in cmake c++ ldd tar sha256sum python3; do command -v "$tool" >/dev/null 2>&1 || { echo "ERROR: required tool not found: $tool" >&2; exit 1; }; done
CORE_BUILD="$ROOT/build_ota_release"
QT_BUILD="$ROOT/qt_app/build_ota_release"
DIST="$ROOT/dist"
PACKAGE="rk3568-spectrum-analyzer-\${VERSION}-linux-aarch64.tar.gz"
mkdir -p "$DIST"
STAGE="$(mktemp -d "$DIST/.stage.XXXXXX")"
cleanup() { rm -rf "$STAGE"; }
trap cleanup EXIT

echo "==> Building core programs ($VERSION, $ARCH)"
cmake -S "$ROOT" -B "$CORE_BUILD" -DCMAKE_BUILD_TYPE=Release
cmake --build "$CORE_BUILD" --target spectrum_batch coupling_batch -j"$BUILD_JOBS"
echo "==> Building Qt application"
cmake -S "$ROOT/qt_app" -B "$QT_BUILD" -DCMAKE_BUILD_TYPE=Release
cmake --build "$QT_BUILD" --target spectrum_qt -j"$BUILD_JOBS"

for name in spectrum_qt spectrum_batch coupling_batch; do
    case "$name" in spectrum_qt) source="$QT_BUILD/$name" ;; *) source="$CORE_BUILD/$name" ;; esac
    [[ -x "$source" ]] || { echo "ERROR: executable missing: $source" >&2; exit 1; }
done

PACKAGE_ROOT="$STAGE/rk3568-spectrum-analyzer-$VERSION"
mkdir -p "$PACKAGE_ROOT/bin"
install -m 755 "$QT_BUILD/spectrum_qt" "$PACKAGE_ROOT/bin/spectrum_qt"
install -m 755 "$CORE_BUILD/spectrum_batch" "$PACKAGE_ROOT/bin/spectrum_batch"
install -m 755 "$CORE_BUILD/coupling_batch" "$PACKAGE_ROOT/bin/coupling_batch"
install -m 644 "$ROOT/VERSION" "$PACKAGE_ROOT/VERSION"
install -m 644 "$ROOT/README.md" "$PACKAGE_ROOT/README.md"
install -m 755 "$ROOT/scripts/install_release.sh" "$PACKAGE_ROOT/install.sh"
install -m 755 "$ROOT/scripts/launch_release.sh" "$PACKAGE_ROOT/launch.sh"

: > "$PACKAGE_ROOT/DEPENDENCIES.txt"
for name in spectrum_qt spectrum_batch coupling_batch; do
    { echo "===== $name ====="; ldd "$PACKAGE_ROOT/bin/$name"; echo; } >> "$PACKAGE_ROOT/DEPENDENCIES.txt"
done
if grep -q "not found" "$PACKAGE_ROOT/DEPENDENCIES.txt"; then echo "ERROR: missing runtime library; see DEPENDENCIES.txt." >&2; exit 1; fi

ARCHIVE="$DIST/$PACKAGE"
tar -czf "$ARCHIVE" -C "$STAGE" "rk3568-spectrum-analyzer-$VERSION"
SIZE_BYTES="$(stat -c '%s' "$ARCHIVE")"
SHA256="$(sha256sum "$ARCHIVE" | awk '{print $1}')"
GIT_COMMIT="$(git -C "$ROOT" rev-parse --short HEAD 2>/dev/null || echo unknown)"
CREATED_UTC="$(date -u +'%Y-%m-%dT%H:%M:%SZ')"

python3 - "$DIST/manifest.json" "$VERSION" "$PACKAGE" "$SIZE_BYTES" "$SHA256" "$GIT_COMMIT" "$CREATED_UTC" <<'PY'
import json
import sys
(output, version, package_file, size, sha256, git_commit, created_utc) = sys.argv[1:]
manifest = {
    "schema_version": 1,
    "product": "RK3568-Spectrum-Analyzer",
    "version": version,
    "platform": "linux",
    "architecture": "aarch64",
    "package_file": package_file,
    "size_bytes": int(size),
    "sha256": sha256,
    "git_commit": git_commit,
    "created_utc": created_utc,
    "release_notes": "Versioned ARM64 application package; user data is kept outside the release directory."
}
with open(output, "w", encoding="utf-8") as f:
    json.dump(manifest, f, ensure_ascii=False, indent=2)
    f.write("\n")
PY

echo
echo "==> Release package created"
echo "Archive : $ARCHIVE"
echo "Manifest: $DIST/manifest.json"
echo "Size    : $SIZE_BYTES bytes"
echo "SHA-256 : $SHA256"
echo "Commit  : $GIT_COMMIT"
echo "Packaging only: no installation or activation was performed."
