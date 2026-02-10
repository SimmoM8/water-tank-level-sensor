# Firmware Build Bootstrap (Arduino CLI)

This project builds with Arduino CLI using a pinned dependency profile.

Source of truth:

- `esp32/level_sensor/sketch.yaml`

Pinned there:

- Board core platform/version (`esp32:esp32`)
- External libraries and versions (`WiFiManager`, `PubSubClient`, `ArduinoJson`)

## Fresh machine bootstrap

1. Install Arduino CLI.
2. From repository root, refresh indexes:

```bash
arduino-cli core update-index
arduino-cli lib update-index
```

3. Compile firmware using the pinned profile:

```bash
arduino-cli compile --profile release esp32/level_sensor
```

4. Optional: run the local two-build matrix check:

```bash
./scripts/build_fw_version_matrix.sh
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
