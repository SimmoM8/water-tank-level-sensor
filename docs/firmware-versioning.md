# Firmware Versioning

## Build-time version

Firmware version is supplied at compile time via `FW_VERSION`.

- Default (when not provided): `"0.0.0-dev"`
- Example override:
  - `-DFW_VERSION=\"1.4.0\"`

Local dual-build check:

```bash
./scripts/build_fw_version_matrix.sh
```

## Home Assistant update entity keys

The MQTT state payload must expose:

- `installed_version`: currently running firmware version
- `latest_version`: OTA target version (falls back to installed when no OTA target is known)

HA discovery update entity reads these via templates:

- `installed_version_template: "{{ value_json.installed_version }}"`
- `latest_version_template: "{{ value_json.latest_version }}"`

