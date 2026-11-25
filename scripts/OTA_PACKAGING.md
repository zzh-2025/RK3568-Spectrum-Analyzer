# ARM64 release packaging (OTA preparation)

This stage creates a versioned application archive. It does not yet download,
install, or roll back updates automatically.

## Build on the RK3568 board

\`\`\`bash
bash scripts/package_release.sh
\`\`\`

The script requires an ARM64 host and builds \`spectrum_qt\`,
\`spectrum_batch\`, and \`coupling_batch\`.

It creates \`dist/rk3568-spectrum-analyzer-<VERSION>-linux-aarch64.tar.gz\`
and \`dist/manifest.json\`. The archive contains a version directory with
\`bin/\`, \`VERSION\`, \`install.sh\`, \`launch.sh\`, and
\`DEPENDENCIES.txt\`. It deliberately excludes \`data/\` and \`results/\`.

## Manual installation test

\`\`\`bash
mkdir -p /tmp/spectrum-ota-test
tar -xzf dist/rk3568-spectrum-analyzer-<VERSION>-linux-aarch64.tar.gz -C /tmp/spectrum-ota-test
cd /tmp/spectrum-ota-test/rk3568-spectrum-analyzer-<VERSION>
bash install.sh
\`\`\`

The installer places releases under
\`~/.local/opt/rk3568-spectrum-analyzer/releases/<VERSION>\` and atomically
updates the \`current\` symlink. Launch with
\`~/.local/opt/rk3568-spectrum-analyzer/current/launch.sh\`.

Installed user data uses Qt's \`QStandardPaths::AppDataLocation\`; its exact
path depends on the XDG data-home setting and Qt organization/application
names. User data is outside the versioned release directory.

## LAN publishing (manual, read-only)

Copy the archive and \`manifest.json\` from \`dist/\` to a folder on the
Windows computer, then serve that folder on the trusted LAN:

\`\`\`powershell
py -m http.server 8000 --directory .\ota_publish
\`\`\`

The future OTA client will fetch the manifest and archive from this server.
SHA-256 currently checks transfer integrity; it is not a digital signature
and does not authenticate the publisher. Use only on a trusted LAN until
signature verification is implemented.
