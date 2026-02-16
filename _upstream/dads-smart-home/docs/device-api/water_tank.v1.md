# Water Tank Device API v1

This document is the v1 contract for telemetry published by firmware on MQTT.

## Topics

- `{baseTopic}/state` (retained): JSON state payload.
- `{baseTopic}/ota/status` (retained): OTA status string.
- `{baseTopic}/ota/progress` (retained): OTA progress integer (`0..100`).
- `{baseTopic}/ack` (not retained): command ACK JSON.

## State Payload (`{baseTopic}/state`)

Top-level keys currently emitted:

- `schema`
- `ts`
- `boot_count`
- `reset_reason`
- `device`
- `fw_version`
- `installed_version`
- `latest_version`
- `update_available`
- `wifi`
- `time`
- `mqtt`
- `probe`
- `calibration`
- `level`
- `config`
- `ota`
- `ota_state`
- `ota_progress`
- `ota_error`
- `ota_target_version`
- `ota_last_ts`
- `ota_last_success_ts`
- `last_cmd`

Notes:

- Fields may be omitted when unavailable (for example ISO8601 timestamp fields before time sync is valid).
- Additional keys may be added in future revisions; consumers should ignore unknown fields.

### Example Payload (abridged)

```json
{
  "schema": 1,
  "ts": 1730000000,
  "boot_count": 42,
  "reset_reason": "panic",
  "device": {
    "id": "water_tank_esp32",
    "name": "Water Tank Sensor",
    "fw": "1.3.0"
  },
  "wifi": { "rssi": -61, "ip": "192.0.2.50" },
  "time": {
    "valid": true,
    "status": "valid",
    "last_attempt_s": 1730000000,
    "last_success_s": 1730000000,
    "next_retry_s": 0
  },
  "mqtt": { "connected": true },
  "probe": { "connected": true, "quality": "ok", "raw": 48012, "raw_valid": true },
  "calibration": { "state": "calibrated", "dry": 41234, "wet": 55321, "inverted": false, "min_diff": 20 },
  "level": {
    "percent": 63.4,
    "percent_valid": true,
    "liters": 13948.0,
    "liters_valid": true,
    "centimeters": 190.2,
    "centimeters_valid": true
  },
  "config": {
    "tank_volume_l": 22000.0,
    "rod_length_cm": 300.0,
    "sense_mode": "touch",
    "simulation_mode": 0
  },
  "ota": {
    "force": false,
    "reboot": true,
    "status": "idle",
    "progress": 0,
    "active": {
      "request_id": "",
      "version": "",
      "url": "",
      "sha256": "",
      "started_ts": 0
    },
    "result": {
      "status": "",
      "message": "",
      "completed_ts": 0
    }
  },
  "ota_state": "idle",
  "ota_progress": 0,
  "ota_error": "",
  "ota_target_version": "",
  "ota_last_ts": "2026-01-01T00:00:00Z",
  "ota_last_success_ts": "2026-01-01T00:00:00Z",
  "update_available": false,
  "last_cmd": {
    "request_id": "auto_0001abcd_00ef",
    "type": "ota_pull",
    "status": "applied",
    "message": "queued",
    "ts": 1730000000
  }
}
```

## OTA Contract

Canonical OTA states:

- `idle`
- `downloading`
- `verifying`
- `applying`
- `rebooting`
- `success`
- `failed`

OTA fields:

- `ota.status`: canonical status enum.
- `ota.progress`: integer `0..100`.
- `ota.active.request_id`: command correlation id for active job.
- `ota.active.version`: target version.
- `ota.active.url`: target URL.
- `ota.active.sha256`: expected SHA256.
- `ota.active.started_ts`: epoch seconds.
- `ota.result.status`: last result category (`success` or `error`).
- `ota.result.message`: last result detail/reason string.
- `ota.result.completed_ts`: epoch seconds.
- `ota_state`: mirror of `ota.status` for compatibility.
- `ota_progress`: mirror of `ota.progress` for compatibility.
- `ota_error`: error summary string.
- `ota_target_version`: manifest target / active target.
- `ota_last_ts`: ISO8601 UTC timestamp.
- `ota_last_success_ts`: ISO8601 UTC timestamp.
- `update_available`: boolean.

Shadow topic values:

- `{baseTopic}/ota/status`: same canonical OTA states as `ota.status`.
- `{baseTopic}/ota/progress`: integer `0..100`.

Reset reason values:

- `power_on`
- `software_reset`
- `panic`
- `deep_sleep`
- `watchdog`
- `other`
