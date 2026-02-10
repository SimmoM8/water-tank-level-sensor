# Firmware Versioning

## Build-time version

Firmware version is supplied at compile time via `FW_VERSION`.

- Default (when not provided): `"0.0.0-dev"`
- Example override:
  - `-DFW_VERSION=\"1.4.0\"`

Local dual-build check:

```bash
./scripts/bootstrap_arduino_cli.sh --install-cli
./scripts/build_fw_version_matrix.sh
```

One-liner (bootstrap + build matrix):

```bash
./scripts/build_fw_version_matrix.sh --bootstrap
```

## Dependency pinning (single source of truth)

Pinned Arduino platform + library versions live in:

- `esp32/level_sensor/sketch.yaml`

Use the profile-based build:

```bash
arduino-cli compile --profile release esp32/level_sensor
```

## Home Assistant update entity keys

The MQTT state payload must expose:

- `installed_version`: currently running firmware version
- `latest_version`: OTA target version (falls back to installed when no OTA target is known)

HA discovery update entity reads these via templates:

- `installed_version_template: "{{ value_json.installed_version }}"`
- `latest_version_template: "{{ value_json.latest_version }}"`

## GitHub release + manifest workflow

Workflow file: `.github/workflows/release-ota.yml`

What it does:

1. Builds firmware `.bin` with `arduino-cli` and `FW_VERSION`.
2. Computes SHA256 of the release binary.
3. Creates/updates a GitHub release tagged `v<version>`.
4. Uploads the firmware `.bin` release asset.
5. Updates `manifests/dev.json` with:
   - `version`
   - `url`
   - `sha256`
6. Commits that manifest update back to `main`.

Required workflow permission:

- `contents: write`

Recommended repo setting:

- Actions workflow permissions: **Read and write**.

Optional secret (only needed if `GITHUB_TOKEN` cannot push to `main` due to branch policy):

- `REPO_PUSH_TOKEN` with `contents:write` on this repository.

### How to trigger

Manual trigger:

1. Open GitHub Actions.
2. Run **Release OTA Firmware**.
3. Provide `version` (for example `1.4.0`).
4. Optionally override `sketch_dir`.

Tag trigger:

```bash
git tag v1.4.0
git push origin v1.4.0
```
