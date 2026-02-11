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
