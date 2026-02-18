# Changes and Differences Since v1.0.3-alpha

**Report Date:** February 18, 2026  
**Last Release:** v1.0.3-alpha (Published: February 18, 2026 at 09:57:51 UTC)  
**Current Branch:** main

## Executive Summary

Since the release of **v1.0.3-alpha**, there has been **1 commit** to the main branch:

- **Manifest Update** (c2062e2): Updated the dev.json manifest to point to the v1.0.3-alpha release

The repository is currently at the same functional state as the v1.0.3-alpha release, with only metadata updates to support OTA distribution.

---

## Changes Since v1.0.3-alpha (Post-Release Updates)

### Commit: c2062e2 - "fix(manifest): update version to 1.0.3-alpha and improve release notes"
**Author:** SimmoM8  
**Date:** February 18, 2026, 09:51:34 UTC  
**Type:** Metadata Update

**Changes:**
- Updated `manifests/dev.json` to reference v1.0.3-alpha firmware
  - Version: `1.0.2-alpha` → `1.0.3-alpha`
  - Firmware URL updated to point to v1.0.3-alpha release assets
  - SHA256 checksum updated: `87dbe659...` → `0b29f6d8...`
  - Release notes improved: "OTA test" → "Official working release with successful OTA updates"

**Impact:**
- Devices on the dev channel will now see v1.0.3-alpha as the available update
- Ensures OTA distribution system points to the latest validated firmware

**Files Modified:**
```diff
manifests/dev.json | 8 ++++----
1 file changed, 4 insertions(+), 4 deletions(-)
```

---

## What's in v1.0.3-alpha Release

For context, here's what the v1.0.3-alpha release itself contains (compared to v1.0.2-alpha):

### Release Information
- **Tag:** v1.0.3-alpha
- **Title:** "v1.0.3-alpha — First successful OTA"
- **Published:** February 18, 2026
- **Status:** Alpha pre-release (not production ready)

### Key Achievement
✅ **First successful end-to-end OTA validation**
- OTA updates now download from GitHub ✓
- SHA256 verification works correctly ✓
- Partition switching functions properly ✓
- Successful reboot and confirmation ✓

### Major Changes (v1.0.2-alpha → v1.0.3-alpha)

The v1.0.3-alpha release included **20 commits** with significant improvements:

#### 1. **OTA Reliability Fixes**
- Increased OTA task stack size with runtime health logging
- Added detailed trace logging for OTA tasks, aborts, and firmware validation
- Improved reboot logging and ensured serial flush before restart
- Fixed firmware image header and content-type validation
- Enhanced cancel-all logic and state clearing

#### 2. **ESP-IDF Native OTA Implementation**
- Removed Arduino `Update.h` dependency
- Migrated to ESP-IDF OTA API exclusively for better control
- Added OTA partition diagnostics and diagnostic event support
- Implemented unpinned OTA task support for single-core/dual-core ESP32

#### 3. **OTA Event System Improvements**
- Unified cancel result emission through ota_events
- Fixed coalesced progress value handling
- Prevented stale progress updates
- Added cancel-all support with queued job purge

#### 4. **Configuration Enhancements**
- Added `CFG_LOG_HIGH_FREQ_DEFAULT` configuration option
- Updated default version handling
- Improved log configuration at boot time

#### 5. **Documentation Updates**
- Clarified `.bin` and manifest file locations
- Documented OTA manifest asset requirements
- Explained dev/stable manifest usage patterns

### Previous Broken Releases (Historical Context)

**v1.0.2-alpha** (BROKEN - DO NOT USE)
- Released: February 17, 2026
- Status: OTA apply failure
- Superseded by: v1.0.3-alpha

**v1.0.1-alpha** (BROKEN - DO NOT USE)
- Released: February 16, 2026
- Status: OTA apply failure
- Superseded by: v1.0.3-alpha

---

## Release Assets

The v1.0.3-alpha release includes:

1. **firmware.bin** (1,163,296 bytes)
   - SHA256: `0b29f6d873b90eeebabc4067f07439560de1f72ae4c2c5beddeccb51759f9e64`
   - Download count: 9
   
2. **dev.json** (306 bytes)
   - SHA256: `337ef02bf507cc13a369e08ac8e319016a8b33cf0508678941334e79c19943ed`
   - Download count: 20

---

## Summary Statistics

### Commits Since v1.0.3-alpha Tag
- **Total commits:** 1
- **Authors:** 1 (SimmoM8)
- **Files changed:** 1 (manifests/dev.json)
- **Lines modified:** 8 (4 insertions, 4 deletions)

### v1.0.3-alpha Release Statistics (from v1.0.2-alpha)
- **Total commits:** 20
- **Files significantly modified:** OTA subsystem, configuration, documentation
- **Major subsystem:** OTA (Over-The-Air updates)

---

## Current Status

✅ **Production Ready for OTA Testing:** v1.0.3-alpha is the first working OTA release  
⚠️ **Alpha Status:** Not recommended for production deployments yet  
🔄 **Active Development:** OTA system stabilization in progress  

### Recommended Actions

1. **For Dev Channel Users:**
   - Update to v1.0.3-alpha to validate OTA functionality
   - Report any OTA issues or failures
   - Monitor update logs for diagnostics

2. **For Stable Channel Users:**
   - Wait for stable release designation
   - Continue using previous stable version
   - Review v1.0.3-alpha release notes before upgrading

3. **For Developers:**
   - Review OTA implementation changes (ESP-IDF migration)
   - Test OTA updates in local environment
   - Validate SHA256 verification workflow

---

## Technical Details

### Manifest Update Diff
```diff
{
  "channel": "dev",
- "version": "1.0.2-alpha",
- "url": "https://github.com/SimmoM8/water-tank-level-sensor/releases/download/v1.0.2-alpha/firmware.bin",
- "sha256": "87dbe65910531986cb20435bcaed8d19aaed2513a964794841f498a971a2c351",
- "notes": "OTA test"
+ "version": "1.0.3-alpha",
+ "url": "https://github.com/SimmoM8/water-tank-level-sensor/releases/download/v1.0.3-alpha/firmware.bin",
+ "sha256": "0b29f6d873b90eeebabc4067f07439560de1f72ae4c2c5beddeccb51759f9e64",
+ "notes": "Official working release with successful OTA updates"
}
```

### Key Commits in v1.0.3-alpha (Top 10)
1. `71a5ee9` - chore(release): bump version to 1.0.3-alpha
2. `c89995b` - fix(config,main,version): add CFG_LOG_HIGH_FREQ_DEFAULT
3. `deba6f2` - fix(ota): increase OTA task stack size, add runtime health logging
4. `061d963` - fix(main,ota_service,wifi_provisioning): improve reboot logging
5. `b6e1afe` - docs(readme): clarify .bin/manifest locations
6. `a963d75` - chore(ota): add detailed trace logging for OTA task
7. `28f6f57` - refactor(ota): support unpinned OTA task for single-core/dual-core
8. `1b4a941` - fix(ota): validate firmware image header and content-type
9. `57e6305` - feat(ota): publish OTA rollback validation diagnostics via MQTT
10. `39e5d40` - refactor(ota): remove Arduino Update.h, use ESP-IDF OTA API

---

## Conclusion

The repository is in a stable state post-v1.0.3-alpha release. The only change since the tag was a necessary manifest update to complete the OTA distribution setup. The v1.0.3-alpha release itself represents a major milestone as the first working OTA implementation, with extensive improvements to reliability, diagnostics, and ESP-IDF integration.

**Next Expected Milestones:**
- Further OTA stability testing
- Bug fixes based on field testing
- Potential promotion to stable channel after validation period
- Additional features and improvements

---

*This summary was generated based on git history analysis and GitHub release information.*
*For detailed commit information, see: `git log v1.0.3-alpha..HEAD`*
