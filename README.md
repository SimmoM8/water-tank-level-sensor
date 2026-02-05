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

**Note**: Firmware binaries (`.bin` files) are **never committed** to this repository. They are distributed via [GitHub Releases](../../releases).

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

### Stable Channel (`stable.json`)
- **Audience**: Production devices deployed in the field
- **Update frequency**: Infrequent, only for tested releases
- **Quality bar**: Thoroughly tested, stable features only
- **Rollout**: Coordinated, monitored deployments

### Dev Channel (`dev.json`)
- **Audience**: Test devices, developer units
- **Update frequency**: As needed for testing new features
- **Quality bar**: Working builds, may have known issues
- **Rollout**: Immediate, no staged deployment

Devices are configured at compile-time or via configuration to check one channel or the other.

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

**Questions?** Open an issue or contact the maintainers.
