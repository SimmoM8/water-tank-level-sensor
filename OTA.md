

# OTA Architecture — Dads Smart Home (ESP32)

This document explains **how remote firmware updates (OTA)** work in this project, why the system is designed the way it is, and how it should be used going forward.

This is written for **future me** (and anyone else touching this code) so the mental model is always clear.

---

## 1. The Core Problem This Solves

The ESP32 water-level sensor must be updateable:

- From overseas
- Without USB access
- Without being on the same LAN
- Without risking bricking the device
- With cryptographic verification
- With minimal Home Assistant configuration

This **rules out** traditional push-based Arduino OTA as the primary solution.

The device must be able to **update itself**.

---

## 2. Design Principle (Most Important)

> **The ESP32 owns its lifecycle.**

Home Assistant (HA):
- Does **not** flash firmware
- Does **not** validate firmware
- Does **not** decide correctness

HA only:
- Sends *intent* (commands)
- Displays *state* (telemetry)

All update logic lives on the device.

---

## 3. High-Level Architecture

```
┌──────────────┐
│   GitHub     │
│              │
│  firmware.bin│
│  manifest.json
└──────┬───────┘
       │ HTTPS (pull)
       ▼
┌────────────────────┐
│ ESP32 (device)     │
│                    │
│ - Checks manifest  │
│ - Downloads binary │
│ - Verifies SHA256  │
│ - Writes flash     │
│ - Reboots safely   │
└──────┬─────────────┘
       │ MQTT
       ▼
┌────────────────────┐
│ Home Assistant     │
│                    │
│ - Sends commands   │
│ - Shows progress   │
│ - Shows errors     │
└────────────────────┘
```

---

## 4. Two OTA Mechanisms (Intentional)

### 4.1 ArduinoOTA (Development Convenience)

- Used when on the **same LAN**
- Push-based
- Manual
- Disabled while pull-OTA is active

This is **not** relied on for production updates.

---

### 4.2 Pull-OTA (Production System)

This is the **real OTA system**.

Characteristics:
- Device-initiated
- HTTPS download
- CA-verified TLS
- SHA256 verified firmware
- Streaming write (low RAM)
- Safe abort on any failure
- Explicit progress & error reporting

This is what enables overseas updates.

---

## 5. Firmware Hosting (GitHub)

The device expects two files hosted over HTTPS:

### 5.1 `manifest.json`

Example:

```json
{
  "version": "1.2.3",
  "url": "https://github.com/<org>/<repo>/releases/download/v1.2.3/firmware.bin",
  "sha256": "64-hex-character-sha256"
}
```

Purpose:
- Declares the latest firmware
- Allows the device to decide if an update is needed
- Prevents HA from needing version logic

---

### 5.2 `firmware.bin`

- Raw ESP32 firmware image
- Served as `application/octet-stream`
- Verified via SHA256 before applying

---

## 6. OTA Flow (Step-by-Step)

### Step 1 — Manifest Check

Triggered by:
- Periodic device check, **or**
- Explicit HA command

Device:
- Downloads `manifest.json`
- Parses version, URL, SHA256
- Compares with `device.fw`

Result:
- `update_available = true | false`

No flashing occurs here.

---

### Step 2 — OTA Start Command

HA sends a command (typically via MQTT Discovery button or Update entity):

```json
{
  "schema": 1,
  "type": "ota_pull",
  "request_id": "uuid",
  "data": {}
}
```

`request_id` is optional for `ota_pull`: if omitted, firmware generates an internal unique ID (`auto_<millis>_<rand>`).  
For non-OTA command types, `request_id` is still required and missing values are rejected with `missing_request_id`.

If `url`/`sha256` are missing, the device uses the manifest URL from config.  
Optional flags can be provided (or stored as defaults): `force`, `reboot`.

---

### Step 3 — Device-Owned OTA Job

Once started, the ESP32:

1. Locks out ArduinoOTA
2. Opens HTTPS connection (CA validated)
3. Follows redirects (GitHub releases)
4. Streams firmware in chunks
5. Writes flash incrementally
6. Computes SHA256 while streaming
7. Verifies hash
8. Applies update
9. Reboots (optional)

If **any step fails**:
- Flash is aborted
- State is marked failed
- Device continues running old firmware

---

### Step 4 — Post-Reboot

After reboot:
- New firmware boots
- Version is updated
- `update_available = false`
- HA reflects new state automatically

---

## 7. Security Model

OTA security relies on **two independent checks**:

1. **TLS (CA verification)**
   - Ensures the firmware came from GitHub
   - Prevents MITM attacks

2. **SHA256 verification**
   - Ensures firmware integrity
   - Prevents corrupted or tampered binaries

Both must pass.

---

## 8. State & Observability

OTA progress is fully observable via MQTT:

- Status states:
  - `idle`
  - `downloading`
  - `verifying`
  - `applying`
  - `success`
  - `failed`
  - `rebooting`

- Progress percentage
- Error reason strings
- Timestamps
- Logs published to `{baseTopic}/event/log`

This allows:
- HA dashboards
- Remote debugging
- Post-mortem analysis

### HA Discovery (What Appears)

Discovery publishes these entities automatically:

- `button.<device>_ota_pull` — triggers OTA pull from manifest
- `sensor.<device>_ota_state` — string state
- `sensor.<device>_ota_progress` — 0–100
- `sensor.<device>_ota_error` — last error string (if any)
- `sensor.<device>_ota_target_version` — latest/target version
- `sensor.<device>_ota_last_ts` — last OTA timestamp (ISO8601 UTC)
- `sensor.<device>_ota_last_success_ts` — last successful OTA timestamp (ISO8601 UTC)
- `sensor.<device>_ota_last_status` / `sensor.<device>_ota_last_message` — last result info
- `binary_sensor.<device>_update_available` — update availability
- `update.<device>_firmware` — HA Update entity (Install triggers `ota_pull`, firmware auto-generates `request_id`)
- `switch.<device>_ota_force` — default force flag
- `switch.<device>_ota_reboot` — default reboot flag

Pressing **Update now** (or the Update entity “Install”) sends `ota_pull` and the device performs the full download/verify/apply flow.

---

## 9. Why This Is Future-Proof

This architecture allows:

- Changing hosting provider (not GitHub-specific)
- Adding delta updates later
- Automatic update schedules
- Rollback strategies
- Multiple device types

Without changing HA.

---

## 10. Rules Going Forward (Important)

- **Do not move OTA logic into Home Assistant**
- **Do not bypass SHA verification**
- **Do not hardcode firmware URLs in HA**
- **Do not refactor OTA unless behavior is preserved**

If confused:
> The device owns its lifecycle.

That rule resolves most design questions.

---

## 11. Quick Mental Summary

- HA says *what*, never *how*
- ESP32 decides if, when, and how to update
- GitHub is just a file host
- OTA is a system, not a feature

End of document.

---

## Troubleshooting (Common Failures)

- `dns_fail` / `tls_fail` / `http_timeout`: network/TLS stage failed. Check DNS, Wi‑Fi stability, clock/time, and CA cert (`ota_ca_cert.h`).
- `http_code_<code>`: HTTP error code from host (404, 403, 429, etc). Confirm release URL and access.
- `bad_content_type` / `content_too_small`: host returned HTML/JSON or tiny body (often rate-limit or error page).
- `sha_mismatch`: manifest SHA does not match firmware.bin (bad upload or wrong hash).
- `download_timeout`: no download progress for 60s.
- `update_end_failed_<code>`: flash write failed; check partition size and free space.

Timestamp format note:
- Sensors with HA `device_class: timestamp` publish ISO8601 UTC strings.
- Numeric `*_s` fields (for example `time.last_attempt_s`) remain epoch seconds.

The device continues running the previous firmware on any failure.
