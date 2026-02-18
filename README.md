# Water Tank Level Sensor - OTA Firmware Distribution

This repository is dedicated to **secure OTA (Over-The-Air) firmware distribution** for ESP32-based water tank level sensors. It contains versioned firmware manifests and references to release binaries—**no application source code**.

> **⚠️ Important**: This is NOT the development repository. All source code, build scripts, and development workflows are maintained in a separate development repository. This repository serves as the **distribution channel** for deployed devices.

## Purpose

This repository provides:
- **Versioned JSON manifests** (`dev` and `stable` channels) that devices check for updates
- **Firmware metadata** including version numbers, checksums, and download URLs
- **Release coordination** linking to firmware binaries hosted as GitHub Releases
- **Security verification** via SHA-256 checksums

This separation enables:
- Secure, controlled firmware distribution without exposing source code
- Independent release cadences for dev testing and stable deployments
- Simplified device update logic (just check manifest endpoints)
- Clear audit trail of what firmware is deployed where

## Repository Structure

```
water-tank-level-sensor/
├── README.md                 # This file
├── RELEASE_PROCESS.md        # Guide for publishing new firmware versions
├── SECURITY.md               # OTA security considerations
├── manifests/
│   ├── dev.json             # Development/testing channel manifest
│   └── stable.json          # Production/stable channel manifest
└── .gitignore               # Prevents committing firmware binaries
```


**Where to Find Files:**
- **Firmware binary (`.bin` file):**
  - After building, the `.bin` file is found in the `build/` or `.pio/build/<env>/` directory of your development environment.
  - For users, the latest `.bin` files are always attached to the corresponding [GitHub Release](../../releases) for each version.
- **Manifest files (`dev.json`, `stable.json`):**
  - These are located in the `manifests/` folder in this repository.
  - Devices fetch these files from GitHub to check for updates.

**Important for OTA:**
- When publishing a new release, you must also upload the updated manifest file (e.g., `stable.json` or `dev.json`) as a release asset in GitHub Releases, alongside the `.bin` file. This ensures devices can access the correct manifest version even if the main branch is delayed or protected.

## OTA Update Flow

### 1. Device Update Check
ESP32 devices periodically check for updates by fetching the appropriate manifest:
```
https://raw.githubusercontent.com/SimmoM8/water-tank-level-sensor/main/manifests/stable.json
```
or for dev devices:
```
https://raw.githubusercontent.com/SimmoM8/water-tank-level-sensor/main/manifests/dev.json
```

### 2. Version Comparison
Device compares its current firmware version against the manifest's `version` field.

### 3. Download Decision
If a newer version is available:
- Device reads the `firmware_url` from the manifest
- Downloads firmware binary via HTTPS from GitHub Releases
- Verifies downloaded file against `sha256` checksum in manifest

### 4. Installation
- Device validates the firmware signature/checksum
- Flashes the new firmware to the ESP32
- Reboots and verifies successful update

### 5. Rollback Safety
- ESP32 OTA typically uses dual partition scheme
- If new firmware fails to boot, device automatically rolls back to previous version

## Manifest Format

Each channel (`dev.json`, `stable.json`) follows this structure:

```json
{
  "version": "1.0.0",
  "firmware_url": "https://github.com/SimmoM8/water-tank-level-sensor/releases/download/v1.0.0/firmware.bin",
  "sha256": "abcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890",
  "size": 1048576,
  "release_notes": "Initial release with basic water level monitoring",
  "min_supported_version": "0.9.0",
  "release_date": "2024-01-15T10:30:00Z"
}
```

### Fields Explained

- **`version`**: Semantic version string (MAJOR.MINOR.PATCH)
- **`firmware_url`**: Direct HTTPS link to firmware binary on GitHub Releases
- **`sha256`**: SHA-256 hash of the firmware binary for integrity verification
- **`size`**: Firmware binary size in bytes (helps device validate download)
- **`release_notes`**: Human-readable description of changes
- **`min_supported_version`**: Oldest firmware version that can directly upgrade to this release
- **`release_date`**: ISO 8601 timestamp of when firmware was published

## Security

### HTTPS Transport
All manifest and firmware downloads occur over **HTTPS**, ensuring:
- Confidentiality (encrypted transmission)
- Authenticity (server certificate validation)
- Integrity (prevents tampering in transit)

### SHA-256 Verification
Devices **must verify** the SHA-256 checksum of downloaded firmware before flashing:
```cpp
if (calculated_sha256 != manifest_sha256) {
    abort_update();
    log_security_event("Checksum mismatch");
}
```

### Manifest Integrity
- Manifests are served from this repository via GitHub's CDN
- Git commit signatures provide authenticity verification
- Only authorized maintainers can update manifests

### Best Practices
- Never commit firmware binaries or secrets to this repository
- Use GitHub Release automation to ensure checksums are generated correctly
- Rotate signing keys if compromise is suspected
- Monitor device update patterns for anomalies

See [SECURITY.md](SECURITY.md) for detailed security considerations.

## Release Channels


### Manifest Files: `stable.json` vs `dev.json`

- **`stable.json`**
  - Used by production devices in the field.
  - Only updated for thoroughly tested, stable releases.
  - Updates are infrequent and coordinated to ensure reliability.
  - Devices using this manifest will only see new updates when they are considered safe for all users.

- **`dev.json`**
  - Used by test devices, developer units, or anyone who wants early access to new features.
  - Updated more frequently, sometimes with experimental or untested changes.
  - Devices using this manifest may receive updates that are not yet fully validated.

**How They Work:**
- Each device is configured (at build time or via settings) to check either the `stable.json` or `dev.json` manifest for updates.
- The manifest tells the device what the latest available firmware is, where to download it, and what version is required to upgrade.
- This allows you to test new firmware on a small group of devices (dev channel) before rolling it out to everyone (stable channel).

**Summary Table:**

| Manifest File      | Audience         | Update Frequency | Stability      | Typical Use                |
|--------------------|------------------|------------------|----------------|----------------------------|
| `stable.json`      | Production users | Low              | High           | Field devices, end users   |
| `dev.json`         | Test/dev users   | High             | Experimental   | Developer/test devices     |

## Publishing New Firmware

**This repository does NOT build firmware.** To publish a new release:

1. **Build firmware** in the development repository
2. **Create a GitHub Release** with the compiled `.bin` file attached
3. **Generate SHA-256 checksum** of the firmware binary
4. **Update manifest** (`dev.json` or `stable.json`) with new version info
5. **Commit and push** manifest changes to this repository
6. **Verify** devices receive the update correctly

See [RELEASE_PROCESS.md](RELEASE_PROCESS.md) for step-by-step instructions.

## Separation from Development Repository

| This Repository (Distribution) | Development Repository |
|-------------------------------|------------------------|
| ✅ JSON manifests | ✅ ESP32 source code (C++, Arduino) |
| ✅ Release documentation | ✅ Build scripts (PlatformIO, Arduino IDE) |
| ✅ Firmware metadata | ✅ Unit tests, integration tests |
| ✅ Security policies | ✅ CI/CD pipelines |
| ❌ Source code | ✅ Development workflows |
| ❌ Build scripts | ✅ Libraries and dependencies |
| ❌ CI/CD | ❌ Firmware manifests (those belong here) |

**Why separate?**
- **Security**: Production devices never need access to source code repositories
- **Clarity**: Each repository has a single, clear purpose
- **Access control**: Different permission models for dev vs. deployment
- **Simplicity**: Devices only fetch small JSON files, not entire repositories

## Device Configuration

ESP32 devices should be configured with:

```cpp
// In your ESP32 OTA code
#define MANIFEST_URL "https://raw.githubusercontent.com/SimmoM8/water-tank-level-sensor/main/manifests/stable.json"
#define UPDATE_CHECK_INTERVAL_MS (24 * 60 * 60 * 1000)  // Check daily
#define CURRENT_VERSION "1.0.0"
```

Alternatively, store the channel selection in EEPROM/NVS so it can be configured without recompilation.

## Contributing

This repository has limited contribution scenarios:

- **Firmware releases**: Follow the release process, update manifests
- **Documentation**: Improve README, security docs, or release guides
- **Manifest format**: Propose changes via issues before modifying structure

**Do NOT**:
- Commit firmware binaries (`.bin`, `.elf`, etc.)
- Add source code or application logic
- Include build tools or CI configurations

## License

[Specify your license here - e.g., MIT, Apache 2.0, etc.]

## Support

For issues related to:
- **Firmware functionality**: Report in the development repository
- **OTA update failures**: Check SECURITY.md, then open an issue here
- **Device configuration**: Refer to device documentation

---


---

# Firmware Update Instructions

## 1. Update/Flash the Device via USB (PlatformIO)

This method is for updating the device by connecting it directly to your computer with a USB cable.

### Step-by-Step (for non-developers):


1. **Install PlatformIO**
   - Download and install [VS Code](https://code.visualstudio.com/) (free code editor).
   - Install the [PlatformIO extension](https://platformio.org/install/ide?install=vscode) in VS Code.

2. **Connect Your Device**
   - Plug your ESP32 device into your computer using a USB cable.

3. **Open the Project**
   - Open the `level_sensor` folder in VS Code.

4. **Build the Firmware**
   - In VS Code, open the PlatformIO sidebar (alien icon).
   - Click **Build** (checkmark icon) to compile the firmware.
   - _What happens:_ PlatformIO checks your code, downloads any needed libraries, and creates a firmware binary for your device.
   - **Where to find the .bin file:** After building, look in `.pio/build/<env>/` (e.g., `.pio/build/arduino_nano_esp32/`). The file will be named something like `firmware.bin` or `level_sensor.ino.bin`.

5. **Upload (Flash) the Firmware**
   - Click **Upload** (right arrow icon) in PlatformIO, or run this command in the VS Code terminal:
     ```bash
     pio run --target upload
     ```
   - _What happens:_ PlatformIO sends the compiled firmware to your ESP32 over USB. The device will reboot and run the new firmware.

6. **Monitor Serial Output (Optional)**
   - Click **Monitor** (plug icon) to see device logs, or run:
     ```bash
     pio device monitor
     ```

**Behind the Scenes:**
- PlatformIO uses the `platformio.ini` file to know which board and settings to use.
- The firmware is compiled for your specific ESP32 model.
- Uploading via USB erases the old firmware and writes the new one directly to the device's memory.

---

## 2. Update the Device Over-the-Air (OTA) from GitHub Releases

This method lets you update the device wirelessly, without unplugging it or using a USB cable. The device downloads new firmware from the internet (GitHub Releases) when an update is available.

### Step-by-Step (for non-developers):

#### A. Preparing and Publishing a New OTA Update


1. **Build the Firmware**
   - Use PlatformIO or Arduino CLI to compile the firmware as above.
   - The result is a `.bin` file (firmware binary).
   - **Where to find the .bin file:** After building, look in `.pio/build/<env>/` (e.g., `.pio/build/arduino_nano_esp32/`).

2. **Create a GitHub Release**
   - Go to the GitHub Releases page for this project.
   - Click **Draft a new release**.
   - Set a version tag (e.g., `v1.2.3` for stable, `v1.2.3-dev` for test/dev).
   - Attach the compiled `.bin` file.
   - **Also attach the updated manifest file** (`stable.json` or `dev.json`) as a release asset. This is required for OTA to work reliably.
   - Add release notes and publish the release.

3. **Generate SHA-256 Checksum**
   - On your computer, run:
     ```bash
     sha256sum firmware.bin
     ```
   - Copy the resulting hash (a long string of letters and numbers).

4. **Update the Manifest File**
   - Edit `manifests/stable.json` (for production) or `manifests/dev.json` (for test/dev).
   - Update the `version`, `firmware_url`, `sha256`, `size`, and `release_date` fields to match your new release.
   - Save and commit the changes, then push to GitHub.
   - **Reminder:** The manifest file you attach to the release should match the one in the repository.

5. **Verify**
   - Check the raw manifest URL in your browser to make sure it shows the new version and checksum.
   - Devices will now see the new update the next time they check for updates.
   - **Where to find the manifest files:** In the `manifests/` folder of this repository, and as assets on the GitHub Release.

**Note:**
- Devices configured for the dev channel will use `dev.json`, and production devices will use `stable.json`. Make sure you update and upload the correct manifest for your target devices.

#### B. How OTA Updates Work (Behind the Scenes)

- The device regularly checks the manifest file on GitHub for new firmware versions.
- If a new version is available, it downloads the firmware binary over HTTPS.
- The device verifies the firmware's SHA-256 checksum to ensure it hasn't been tampered with.
- If the check passes, the device flashes the new firmware and reboots.
- If the update fails (bad download, power loss, etc.), the device will roll back to the previous working firmware.

#### C. Ways to Manually Trigger an OTA Update

1. **Home Assistant (HA) Integration**
  - If your device is integrated with Home Assistant, you can trigger an OTA update from the HA dashboard (e.g., via a button or service call).
  - _What happens:_ HA sends a command to the device over MQTT to start the update process immediately.

2. **Serial Command**
  - Connect to the device via USB and open a serial terminal.
  - Send the appropriate command (see device documentation) to trigger an OTA update.

3. **MQTT Command**
  - Publish a special message to the device's MQTT command topic to request an update.

4. **Automatic Check**
  - Devices check for updates automatically at regular intervals (e.g., every 24 hours).

**Note:** The device will only update if the new firmware version is higher than the current one, and if the update is allowed by the manifest's `min_supported_version`.

---

## Troubleshooting

- If USB upload fails, check your cable, drivers, and that the correct board is selected in PlatformIO.
- If OTA update fails, check device logs for errors (checksum mismatch, download failure, etc.).
- Make sure the manifest and firmware URL are correct and public.
- For more help, see [RELEASE_PROCESS.md](RELEASE_PROCESS.md) and [SECURITY.md](SECURITY.md).

---

**Still have questions?** Open an issue or contact the maintainers.
