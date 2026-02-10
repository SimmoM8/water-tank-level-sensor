# Firmware Build Bootstrap (Arduino CLI)

This project builds with Arduino CLI using a pinned dependency profile.

Source of truth:

- `esp32/level_sensor/sketch.yaml`

Pinned there:

- Board core platform/version (`esp32:esp32`)
- External libraries and versions (`WiFiManager`, `PubSubClient`, `ArduinoJson`)

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
  --profile release \
  --build-property 'compiler.cpp.extra_flags=-DFW_VERSION=\"1.4.0\"' \
  --output-dir build \
  esp32/level_sensor
```

CI uses the same `release` profile from `esp32/level_sensor/sketch.yaml`.
