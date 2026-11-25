# ARM64 release packaging and OTA

The OTA client supports LAN manifest checks, package download, file-size and
SHA-256 verification, user-confirmed installation, atomic `current` symlink
activation, startup health checks, and rollback to the previous release if the
new GUI fails to start. The complete upgrade and rollback flow still requires
on-board validation.

## Build on the RK3568 board

Run this from the repository root:

```bash
bash scripts/package_release.sh
```

The script requires an ARM64 host and builds `spectrum_qt`,
`spectrum_batch`, and `coupling_batch`.

It creates
`dist/rk3568-spectrum-analyzer-<VERSION>-linux-aarch64.tar.gz` and
`dist/manifest.json`. The archive contains a version directory with
`bin/`, `VERSION`, `install.sh`, `launch.sh`,
`ota_apply_update.sh`, and `DEPENDENCIES.txt`. It deliberately excludes
`data/` and `results/`.

## First-time installation on the board

The Qt install action is enabled only when the application is running from
the managed installation path. Install the baseline release once:

```bash
mkdir -p /tmp/spectrum-baseline
tar -xzf dist/rk3568-spectrum-analyzer-1.0.0-linux-aarch64.tar.gz -C /tmp/spectrum-baseline
cd /tmp/spectrum-baseline/rk3568-spectrum-analyzer-1.0.0
bash install.sh
```

This installs releases under
`~/.local/opt/rk3568-spectrum-analyzer/releases/<VERSION>` and atomically
updates `~/.local/opt/rk3568-spectrum-analyzer/current`. Close any development
build window and launch the managed version with:

```bash
~/.local/opt/rk3568-spectrum-analyzer/current/launch.sh
```

Installed user data uses Qt's `QStandardPaths::AppDataLocation`; its exact
path depends on the XDG data-home setting and Qt organization/application
names. User data is outside the versioned release directory.

## LAN publishing (manual, read-only)

Copy the archive and `manifest.json` from `dist/` to a folder on the
Windows computer, then serve that folder on the trusted LAN:

```powershell
py -m http.server 8000 --directory .\ota_publish
```

Open the Qt “软件更新” tab, enter
`http://<Windows-LAN-IP>:8000`, and click “检查更新”. If a newer version
is available, click “下载并校验安装包”. After the size and SHA-256 checks
pass, the same button changes to “安装更新并重启”. Confirm the prompt to
begin the update.

The independent updater waits for the old process to exit, rechecks archive
size and SHA-256, validates archive paths and required executables, extracts
the new release, and atomically switches the `current` symlink. It launches
the new GUI with `SPECTRUM_OTA_HEALTH_FILE`; the application writes its
version marker after the main window is shown and the Qt event loop has run
for three seconds. If the marker is missing, incorrect, or the new process
exits during the health window, the updater restores the previous `current`
symlink and relaunches the old release.

Update logs are written to
`~/.local/opt/rk3568-spectrum-analyzer/ota-update.log`. Old release
directories are retained so the previous version remains available. The
updater does not modify the persistent user data directory.

SHA-256 checks transfer integrity; it is not a digital signature and does
not authenticate the publisher. Use only on a trusted LAN until signature
verification is implemented.
