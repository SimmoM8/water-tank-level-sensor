{
  "schema_version": 1,
  "device": {
    "id": "water_tank_esp32",
    "name": "Water Tank Sensor",
    "sw": "1.3",
    "uptime_ms": 1234567,
    "rssi": -61
  },
  "time": {
    "ts_ms": 1730000000000
  },
  "health": {
    "status": "online",
    "probe_connected": true,
    "quality_reason": "ok",
    "errors": [],
    "warnings": []
  },
  "active": {
    "calibration": { "dry": 41234, "wet": 55321, "inverted": false },
    "tank": { "tank_length_cm": 300, "volume_l": 22000 },
    "simulation": { "enabled": false, "mode": 0 }
  },
  "readings": {
    "raw": 48012,
    "percent": 63.4,
    "height_cm": 190.2,
    "liters": 13948,
    "valid": { "raw": true, "percent": true, "height_cm": true, "liters": true }
  },
  "last_cmd": {
    "id": "b7f4a6",
    "type": "set_calibration_dry",
    "requested": { "dry": 41234 },
    "active": true
  }
}