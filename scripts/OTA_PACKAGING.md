# ARM64 release packaging (OTA preparation)

The current implementation creates versioned ARM64 packages and provides a Qt page for LAN manifest checks, package download, file-size checks, and SHA-256 verification. Automatic installation, activation, health checks, and rollback are the next stage.

## Build on the RK3568 board

```bash
bash scripts/package_release.sh
```

The script requires an ARM64 host and builds `spectrum_qt`,
`spectrum_batch`, and `coupling_batch`.

It creates `dist/rk3568-spectrum-analyzer-<VERSION>-linux-aarch64.tar.gz`
and `dist/manifest.json`. The archive contains a version directory with
`bin/`, `VERSION`, `install.sh`, `launch.sh`, and
`DEPENDENCIES.txt`. It deliberately excludes `data/` and `results/`.

## Manual installation test

```bash
mkdir -p /tmp/spectrum-ota-test
tar -xzf dist/rk3568-spectrum-analyzer-<VERSION>-linux-aarch64.tar.gz -C /tmp/spectrum-ota-test
cd /tmp/spectrum-ota-test/rk3568-spectrum-analyzer-<VERSION>
bash install.sh
```

The installer places releases under
`~/.local/opt/rk3568-spectrum-analyzer/releases/<VERSION>` and atomically
updates the `current` symlink. Launch with
`~/.local/opt/rk3568-spectrum-analyzer/current/launch.sh`.

Installed user data uses Qt's `QStandardPaths::AppDataLocation`; its exact
path depends on the XDG data-home setting and Qt organization/application
names. User data is outside the versioned release directory.

## LAN publishing (manual, read-only)

Copy the archive and `manifest.json` from `dist/` to a folder on the
Windows computer, then serve that folder on the trusted LAN:

```powershell
py -m http.server 8000 --directory .\ota_publish
```

In the current Qt application, open the “软件更新” tab, keep the server URL as `http://<Windows-LAN-IP>:8000`, and click “检查更新”. If the server version is newer, click “下载并校验安装包”. The downloaded archive is stored under Qt's application cache directory in `ota-downloads/`.

The client validates the product, platform, architecture, version, package filename, package size, and SHA-256 format before download. It then checks the downloaded byte count and SHA-256. This stage does not install or activate the package yet.

SHA-256 currently checks transfer integrity; it is not a digital signature and does not authenticate the publisher. Use only on a trusted LAN until signature verification is implemented.
