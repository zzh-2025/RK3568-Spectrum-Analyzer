#!/usr/bin/env bash
set -Eeuo pipefail

if [[ "$#" -ne 5 ]]; then
    echo "Usage: ota_apply_update.sh ARCHIVE VERSION SHA256 SIZE_BYTES OLD_PID" >&2
    exit 2
fi

ARCHIVE="$1"
EXPECTED_VERSION="$2"
EXPECTED_SHA256="$3"
EXPECTED_SIZE="$4"
OLD_PID="$5"

INSTALL_ROOT="$HOME/.local/opt/rk3568-spectrum-analyzer"
RELEASES="$INSTALL_ROOT/releases"
CURRENT="$INSTALL_ROOT/current"
LOG_FILE="$INSTALL_ROOT/ota-update.log"
LOCK_DIR="$INSTALL_ROOT/.ota-update.lock"
EXPECTED_TOP="rk3568-spectrum-analyzer-$EXPECTED_VERSION"

mkdir -p "$INSTALL_ROOT" "$RELEASES"
exec >> "$LOG_FILE" 2>&1

log() {
    printf '[%s] %s\n' "$(date '+%F %T')" "$*"
}

die() {
    log "ERROR: $*"
    exit 1
}

OLD_TARGET=""
OLD_RELEASE=""
SWITCHED=0
NEW_PID=""
STAGE=""
HEALTH_MARKER=""
LOCK_ACQUIRED=0

on_exit() {
    local rc=$?
    trap - EXIT

    if [[ "$rc" -ne 0 ]]; then
        log "OTA failed; restoring the previous application state."

        if [[ -n "$NEW_PID" ]] && kill -0 "$NEW_PID" 2>/dev/null; then
            kill "$NEW_PID" 2>/dev/null || true
            sleep 1
        fi

        if [[ "$SWITCHED" -eq 1 && -n "$OLD_TARGET" ]]; then
            local rollback_link="$INSTALL_ROOT/.current.rollback.$$"
            rm -f "$rollback_link"
            if ln -s "$OLD_TARGET" "$rollback_link" && mv -Tf "$rollback_link" "$CURRENT"; then
                log "Restored current -> $OLD_TARGET"
            else
                log "CRITICAL: automatic symlink rollback failed."
            fi
        fi

        if [[ -n "$OLD_RELEASE" && -x "$OLD_RELEASE/launch.sh" ]]; then
            if [[ -z "$OLD_PID" ]] || ! kill -0 "$OLD_PID" 2>/dev/null; then
                log "Restarting previous release: $OLD_RELEASE"
                "$OLD_RELEASE/launch.sh" >> "$LOG_FILE" 2>&1 &
            fi
        fi
    fi

    if [[ -n "$STAGE" && -d "$STAGE" ]]; then
        rm -rf "$STAGE"
    fi
    if [[ -n "$HEALTH_MARKER" ]]; then
        rm -f "$HEALTH_MARKER"
    fi
    if [[ "$LOCK_ACQUIRED" -eq 1 ]]; then
        rmdir "$LOCK_DIR" 2>/dev/null || true
    fi
}

trap on_exit EXIT

if [[ ! -L "$CURRENT" ]]; then
    die "No managed current release found. Install the baseline release first."
fi

OLD_TARGET="$(readlink "$CURRENT")"
OLD_RELEASE="$(readlink -f "$CURRENT")"
if [[ -z "$OLD_RELEASE" || ! -x "$OLD_RELEASE/launch.sh" ]]; then
    die "The current release is invalid: $CURRENT"
fi
case "$OLD_RELEASE" in
    "$RELEASES"/*) ;;
    *) die "The current release points outside the managed releases directory." ;;
esac

if ! mkdir "$LOCK_DIR" 2>/dev/null; then
    die "Another OTA operation is running, or stale lock exists: $LOCK_DIR"
fi
LOCK_ACQUIRED=1

if [[ ! "$EXPECTED_VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    die "Invalid target version: $EXPECTED_VERSION"
fi
if [[ ! "$EXPECTED_SHA256" =~ ^[0-9a-fA-F]{64}$ ]]; then
    die "Invalid SHA-256 value."
fi
if [[ ! "$EXPECTED_SIZE" =~ ^[0-9]+$ ]] || [[ "$EXPECTED_SIZE" -le 0 ]]; then
    die "Invalid expected package size."
fi
if [[ ! "$OLD_PID" =~ ^[0-9]+$ ]]; then
    die "Invalid old application PID."
fi
if [[ ! -f "$ARCHIVE" ]]; then
    die "Downloaded package does not exist: $ARCHIVE"
fi

ACTUAL_SIZE="$(stat -c '%s' "$ARCHIVE")"
[[ "$ACTUAL_SIZE" == "$EXPECTED_SIZE" ]] || die "Package size mismatch: expected $EXPECTED_SIZE, got $ACTUAL_SIZE."
ACTUAL_SHA256="$(sha256sum "$ARCHIVE" | awk '{print $1}')"
[[ "$ACTUAL_SHA256" == "$EXPECTED_SHA256" ]] || die "Package SHA-256 mismatch."

tar -tzf "$ARCHIVE" >/dev/null || die "Package is not a valid gzip tar archive."
while IFS= read -r entry; do
    [[ "$entry" != /* ]] || die "Archive contains an absolute path: $entry"
    case "$entry" in
        ..|../*|*/..|*/../*) die "Archive contains a path traversal entry: $entry" ;;
    esac
    case "$entry" in
        "$EXPECTED_TOP"|"$EXPECTED_TOP/"*) ;;
        *) die "Unexpected archive root or path: $entry" ;;
    esac
done < <(tar -tzf "$ARCHIVE")

log "Verified package integrity for version $EXPECTED_VERSION."

for ((i = 0; i < 30; ++i)); do
    if ! kill -0 "$OLD_PID" 2>/dev/null; then
        break
    fi
    sleep 1
done
if kill -0 "$OLD_PID" 2>/dev/null; then
    die "The old application did not exit within 30 seconds."
fi

STAGE="$(mktemp -d "$RELEASES/.ota-$EXPECTED_VERSION.XXXXXX")"
tar -xzf "$ARCHIVE" -C "$STAGE" --no-same-owner --no-same-permissions
NEW_ROOT="$STAGE/$EXPECTED_TOP"

[[ -d "$NEW_ROOT" ]] || die "Expected package directory is missing."
[[ -f "$NEW_ROOT/VERSION" ]] || die "VERSION file is missing."
PACKAGE_VERSION="$(tr -d '\r\n' < "$NEW_ROOT/VERSION")"
[[ "$PACKAGE_VERSION" == "$EXPECTED_VERSION" ]] || die "VERSION file does not match the manifest."
for name in spectrum_qt spectrum_batch coupling_batch; do
    [[ -x "$NEW_ROOT/bin/$name" ]] || die "Missing executable: bin/$name"
    if ldd "$NEW_ROOT/bin/$name" 2>&1 | grep -q "not found"; then
        die "Runtime dependency missing for $name."
    fi
done
[[ -x "$NEW_ROOT/launch.sh" ]] || die "launch.sh is missing or not executable."
[[ -x "$NEW_ROOT/ota_apply_update.sh" ]] || die "ota_apply_update.sh is missing or not executable."

DEST="$RELEASES/$EXPECTED_VERSION"
if [[ -e "$DEST" || -L "$DEST" ]]; then
    DEST_REAL="$(readlink -f "$DEST" 2>/dev/null || true)"
    [[ "$DEST_REAL" != "$OLD_RELEASE" ]] || die "Target version is already the active release."
    rm -rf "$DEST"
fi

mv "$NEW_ROOT" "$DEST"
rm -rf "$STAGE"
STAGE=""

LINK_TMP="$INSTALL_ROOT/.current.new.$$"
rm -f "$LINK_TMP"
ln -s "releases/$EXPECTED_VERSION" "$LINK_TMP"
mv -Tf "$LINK_TMP" "$CURRENT"
SWITCHED=1
log "Activated candidate release $EXPECTED_VERSION; starting health check."

HEALTH_MARKER="$INSTALL_ROOT/.ota-health-$EXPECTED_VERSION-$$"
rm -f "$HEALTH_MARKER"
SPECTRUM_OTA_HEALTH_FILE="$HEALTH_MARKER" "$CURRENT/launch.sh" >> "$LOG_FILE" 2>&1 &
NEW_PID=$!

HEALTH_OK=0
for ((i = 0; i < 30; ++i)); do
    if [[ -s "$HEALTH_MARKER" ]]; then
        HEALTH_OK=1
        break
    fi
    if ! kill -0 "$NEW_PID" 2>/dev/null; then
        die "The new application exited before reporting healthy startup."
    fi
    sleep 1
done

[[ "$HEALTH_OK" -eq 1 ]] || die "The new application did not pass startup health check within 30 seconds."
HEALTH_VERSION="$(tr -d '\r\n' < "$HEALTH_MARKER")"
[[ "$HEALTH_VERSION" == "$EXPECTED_VERSION" ]] || die "Health check version mismatch: $HEALTH_VERSION."

sleep 3
kill -0 "$NEW_PID" 2>/dev/null || die "The new application exited immediately after startup health check."

log "OTA SUCCESS: version $EXPECTED_VERSION is active and passed startup health check."
exit 0
