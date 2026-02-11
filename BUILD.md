# Firmware Build Bootstrap (Arduino CLI)

This project builds with Arduino CLI using a pinned dependency profile.

Source of truth:

- `esp32/level_sensor/sketch.yaml`

Pinned there:

- Board core platform/version (`esp32:esp32`)
- External libraries and versions (`WiFiManager`, `PubSubClient`, `ArduinoJson`)

Release workflow base board:

- FQBN from sketch profile: `esp32:esp32:esp32`
- CI then pins board options explicitly for deterministic OTA builds:
  - `PartitionScheme=default` (dual OTA app slots)
  - `FlashSize=4M`
  - `FlashMode=qio`
  - `FlashFreq=80`
  - `PSRAM=disabled`

OTA size budget enforced in CI:

- Max firmware size: `1,310,720` bytes (`0x140000`) to fit a single OTA app slot.
- Build fails if firmware exceeds this slot size.

## Fresh machine bootstrap

1. Run bootstrap from repository root:

```bash
./scripts/bootstrap_arduino_cli.sh --install-cli
```

2. Run the local build matrix:

```bash
./scripts/build_fw_version_matrix.sh
```

Optional one-liner:

```bash
./scripts/build_fw_version_matrix.sh --bootstrap
```

## Release build (versioned)

```bash
arduino-cli compile \
  --fqbn "esp32:esp32:esp32:FlashMode=qio,FlashFreq=80,FlashSize=4M,PartitionScheme=default,PSRAM=disabled" \
  --build-property 'compiler.cpp.extra_flags=-DFW_VERSION=\"1.4.0\"' \
  --output-dir build \
  --export-binaries \
  esp32/level_sensor
```

CI uses the same `release` profile from `esp32/level_sensor/sketch.yaml`.

## Firmware Build Audit Script

Run a full compile + static audit (size, symbol/map, dynamic allocation indicators, format-string risk, JSON headroom):

```bash
./scripts/audit_firmware_build.py
```

Strict mode (non-zero exit on warnings/findings):

```bash
./scripts/audit_firmware_build.py --strict
```

Reports are written to:

- `build/firmware_audit/reports/audit_summary.txt`
- `build/firmware_audit/reports/compile.log`
- `build/firmware_audit/reports/symbols_nm.txt`
- `build/firmware_audit/reports/size.txt`
- `build/firmware_audit/reports/size_sections.txt`
- `build/firmware_audit/reports/symbols_objdump.txt`
