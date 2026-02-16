# Release Process

This document describes how to publish new firmware versions to the OTA distribution system.

## Prerequisites

Before publishing a firmware release:
- ✅ Firmware has been built and tested in the development repository
- ✅ You have maintainer access to this repository
- ✅ You have appropriate permissions to create GitHub Releases
- ✅ Firmware binary has been tested on physical hardware

## Step-by-Step Release Process

### 1. Build Firmware in Development Repository

In your development repository (NOT this one):

```bash
# Build the firmware using your build system (PlatformIO, Arduino CLI, etc.)
pio run --target release

# Or with Arduino CLI
arduino-cli compile --fqbn esp32:esp32:esp32 --output-dir ./build
```

Locate the compiled firmware binary (typically `firmware.bin` or similar).

### 2. Generate SHA-256 Checksum

Calculate the SHA-256 hash of your firmware binary:

**On Linux/macOS:**
```bash
sha256sum firmware.bin
# Output: abcdef1234567890... firmware.bin
```

**On Windows (PowerShell):**
```powershell
Get-FileHash -Algorithm SHA256 firmware.bin
```

**Save this hash** - you'll need it for the manifest update.

### 3. Get File Size

**On Linux/macOS:**
```bash
stat -f%z firmware.bin  # macOS
stat -c%s firmware.bin  # Linux
```

**On Windows:**
```powershell
(Get-Item firmware.bin).length
```

### 4. Create GitHub Release

1. Go to the [Releases page](../../releases) of this repository
2. Click **"Draft a new release"**
3. Fill in the release details:
   - **Tag**: Use semantic versioning (e.g., `v1.2.3` for stable, `v1.2.3-dev` for dev)
   - **Release title**: Same as tag or descriptive (e.g., "Version 1.2.3 - Bug Fixes")
   - **Description**: Include release notes, changes, known issues
   - **Attach binary**: Upload your `firmware.bin` file
4. **For dev releases**: Check "This is a pre-release"
5. Click **"Publish release"**

### 5. Update Manifest

Choose which channel to update:

#### For Stable Releases (Production Devices)

Edit `manifests/stable.json`:

```json
{
  "version": "1.2.3",
  "firmware_url": "https://github.com/SimmoM8/water-tank-level-sensor/releases/download/v1.2.3/firmware.bin",
  "sha256": "YOUR_CALCULATED_SHA256_HERE",
  "size": 1048576,
  "release_notes": "Fixed temperature sensor calibration, improved WiFi reconnection logic",
  "min_supported_version": "1.0.0",
  "release_date": "2026-02-05T10:30:00Z"
}
```

#### For Development Releases (Test Devices)

Edit `manifests/dev.json`:

```json
{
  "version": "1.3.0-dev",
  "firmware_url": "https://github.com/SimmoM8/water-tank-level-sensor/releases/download/v1.3.0-dev/firmware.bin",
  "sha256": "YOUR_CALCULATED_SHA256_HERE",
  "size": 1050000,
  "release_notes": "Testing new ultrasonic sensor driver (experimental)",
  "min_supported_version": "1.2.0",
  "release_date": "2026-02-05T14:00:00Z"
}
```

**Important fields:**
- `version`: Must follow semantic versioning (MAJOR.MINOR.PATCH[-prerelease])
- `firmware_url`: Full HTTPS URL to the binary in the GitHub Release
- `sha256`: **Must match** the checksum you calculated
- `size`: Exact file size in bytes
- `min_supported_version`: Oldest version that can upgrade (for migration safety)
- `release_date`: ISO 8601 format timestamp (UTC)

### 6. Commit and Push Manifest

```bash
git add manifests/stable.json  # or dev.json
git commit -m "Release v1.2.3: Bug fixes and improvements"
git push origin main
```

### 7. Verify Deployment

After pushing the manifest:

1. **Check manifest URL**: Visit the raw GitHub URL to confirm changes are live:
   - Stable: `https://raw.githubusercontent.com/SimmoM8/water-tank-level-sensor/main/manifests/stable.json`
   - Dev: `https://raw.githubusercontent.com/SimmoM8/water-tank-level-sensor/main/manifests/dev.json`

2. **Test with a device**: If possible, trigger an update check on a test device

3. **Monitor device logs**: Watch for successful downloads and installations

4. **Check for errors**: Look for checksum mismatches, download failures, or boot loops

### 8. Rollback (If Needed)

If a release causes issues:

1. **Immediate**: Revert the manifest to the previous version:
   ```bash
   git revert HEAD
   git push origin main
   ```

2. **Devices will stop updating** to the problematic version

3. **Investigate**: Fix the firmware issue in the development repository

4. **Do NOT delete the GitHub Release** - this breaks URLs for devices mid-download

## Release Checklist

Use this checklist for every release:

- [ ] Firmware built and tested on hardware
- [ ] SHA-256 checksum calculated and verified
- [ ] File size recorded
- [ ] GitHub Release created with correct tag and binary
- [ ] Manifest updated with correct URL, checksum, and metadata
- [ ] Manifest committed and pushed to repository
- [ ] Raw manifest URL checked in browser (force refresh)
- [ ] Test device successfully updated (if available)
- [ ] Release notes documented in manifest and GitHub Release

## Versioning Guidelines

### Semantic Versioning (SemVer)

Follow [Semantic Versioning 2.0.0](https://semver.org/):

- **MAJOR** (1.x.x): Breaking changes, incompatible with previous versions
- **MINOR** (x.1.x): New features, backward compatible
- **PATCH** (x.x.1): Bug fixes, backward compatible

### Version Examples

- `1.0.0` - Initial stable release
- `1.1.0` - Added new sensor support (backward compatible)
- `1.1.1` - Fixed WiFi reconnection bug
- `2.0.0` - Changed sensor protocol (requires device reconfiguration)
- `1.2.0-dev` - Development build for testing new feature
- `2.0.0-beta.1` - Beta version of major release

### Min Supported Version

The `min_supported_version` field prevents unsafe upgrades:

- If device is running `0.8.0` and manifest requires `1.0.0`, update is blocked
- Use this when:
  - New firmware requires EEPROM/NVS migrations
  - WiFi configuration format changed
  - Bootloader must be updated first

Example:
```json
{
  "version": "2.0.0",
  "min_supported_version": "1.5.0",
  "release_notes": "Requires EEPROM migration from 1.5.0+. If running older version, update to 1.5.0 first."
}
```

## Channel Strategy

### When to Use Dev Channel

- Testing new features before stable release
- Validating bug fixes on limited devices
- Experimenting with breaking changes
- Frequent updates for active development

**Recommended**: Keep a few dedicated test devices subscribed to dev channel

### When to Use Stable Channel

- Deploying to production devices in the field
- After thorough testing in dev channel
- Only for well-tested, stable firmware
- Infrequent updates (e.g., quarterly or as needed)

**Best practice**: Let firmware "bake" in dev channel for at least a week before stable

## Automation Ideas (Optional)

Consider automating parts of this process:

1. **GitHub Actions**: Automatically generate SHA-256 when creating a release
2. **Release scripts**: Automate manifest updates with validated checksums
3. **Notification system**: Alert when new firmware is published
4. **Monitoring**: Track device update success rates

However, **keep manual approval** for stable releases to prevent accidental deploys.

## Troubleshooting

### Devices Not Updating

- ✅ Verify manifest URL is accessible (check in browser)
- ✅ Confirm version number increased (devices only update to newer versions)
- ✅ Check device logs for errors (network, checksum, partition space)
- ✅ Ensure GitHub Release is public (not draft)
- ✅ Verify firmware URL in manifest is correct (typos, wrong tag)

### Checksum Mismatch Errors

- ❌ SHA-256 in manifest doesn't match actual firmware binary
- ✅ Recalculate checksum and update manifest
- ✅ Ensure you're hashing the exact binary uploaded to GitHub Release
- ✅ Check for trailing whitespace or invisible characters in manifest

### Download Failures

- ✅ Confirm firmware binary is attached to release (not deleted)
- ✅ Check GitHub Release is public
- ✅ Verify firmware URL uses `github.com`, not `raw.githubusercontent.com`
- ✅ Test downloading the firmware URL manually with `curl` or `wget`

### OTA Update Fails to Boot

- ✅ Ensure firmware was built for correct ESP32 variant (ESP32, ESP32-S2, etc.)
- ✅ Check partition table compatibility
- ✅ Verify bootloader version requirements
- ✅ Test firmware on hardware before releasing to stable

## Security Reminders

- ⚠️ **Never commit firmware binaries** to Git (use `.gitignore`)
- ⚠️ **Always verify checksums** before publishing manifest
- ⚠️ **Use HTTPS URLs only** (never HTTP)
- ⚠️ **Restrict write access** to this repository (maintainers only)
- ⚠️ **Review manifest changes** in pull requests before merging

---

**Questions?** Open an issue or contact the repository maintainers.
