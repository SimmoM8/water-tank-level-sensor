# Security Policy

This document outlines the security considerations and best practices for the OTA firmware distribution system.

## Security Model

The security of this OTA update system relies on multiple layers:

1. **Transport Security (HTTPS)**: All manifest and firmware downloads use encrypted HTTPS
2. **Integrity Verification (SHA-256)**: Firmware checksums prevent tampering
3. **Platform Security (GitHub)**: Manifests and releases hosted on trusted infrastructure
4. **Code Signing (Optional)**: Additional signature verification can be implemented
5. **Rollback Protection**: ESP32 dual-partition scheme enables safe fallback

## Threat Model

### Threats Mitigated

✅ **Man-in-the-Middle (MITM) Attacks**
- HTTPS encryption prevents interception and modification of downloads
- ESP32 validates server certificates

✅ **Firmware Tampering**
- SHA-256 checksum verification ensures firmware hasn't been modified
- Devices reject firmware with mismatched checksums

✅ **Unauthorized Releases**
- GitHub access controls limit who can create releases
- Git commit signatures verify manifest authenticity

✅ **Partial Download Corruption**
- File size validation ensures complete download
- Checksum verification catches any corrupted bytes

✅ **Rollback to Vulnerable Versions**
- `min_supported_version` prevents downgrade attacks
- Devices maintain previous working firmware in backup partition

### Threats Requiring Additional Mitigation

⚠️ **Compromise of GitHub Account**
- **Risk**: Attacker with access could publish malicious firmware
- **Mitigation**: 
  - Enable 2FA on all maintainer accounts
  - Use branch protection rules
  - Require signed commits
  - Monitor release activity
  - Implement code signing (see below)

⚠️ **Supply Chain Attacks (Development)**
- **Risk**: Malicious code introduced during firmware development
- **Mitigation**: 
  - Secure development repository separately
  - Code review all changes
  - Scan dependencies for vulnerabilities
  - Use trusted build environments

⚠️ **Device Key Compromise**
- **Risk**: WiFi credentials or device keys extracted from firmware
- **Mitigation**: 
  - Encrypt sensitive data in firmware
  - Use secure storage (ESP32 flash encryption, secure boot)
  - Never hardcode credentials

⚠️ **Replay Attacks**
- **Risk**: Old firmware packages re-served to devices
- **Mitigation**: 
  - Version checking prevents downgrade
  - Monitor device update patterns
  - Use `min_supported_version` strategically

## Implementation Security Checklist

### For ESP32 Device Firmware

- [ ] **Validate server certificates** during HTTPS requests
  ```cpp
  // Use GitHub's root certificates
  const char* github_root_ca = "-----BEGIN CERTIFICATE-----...";
  client.setCACert(github_root_ca);
  ```

- [ ] **Verify SHA-256 checksum** before flashing
  ```cpp
  esp_partition_t* partition = esp_ota_get_next_update_partition(NULL);
  esp_ota_handle_t ota_handle;
  
  // Download and hash simultaneously
  // After download completes:
  if (calculated_hash != manifest_hash) {
      esp_ota_abort(ota_handle);
      log_error("Checksum mismatch - aborting update");
      return;
  }
  ```

- [ ] **Check version before downloading**
  ```cpp
  bool should_update = compare_versions(current_version, manifest_version);
  bool supported = compare_versions(current_version, min_supported_version) >= 0;
  
  if (!should_update || !supported) {
      log_info("No applicable update available");
      return;
  }
  ```

- [ ] **Implement timeout and retry logic**
  ```cpp
  #define OTA_TIMEOUT_MS 300000  // 5 minutes
  #define OTA_MAX_RETRIES 3
  ```

- [ ] **Log security events**
  - Checksum mismatches
  - Certificate validation failures
  - Unexpected download interruptions
  - Version anomalies

- [ ] **Use ESP32 secure features** (recommended for production)
  - Enable flash encryption
  - Enable secure boot
  - Lock bootloader for write protection

### For Repository Maintainers

- [ ] **Enable 2FA** on GitHub accounts with write access
- [ ] **Protect main branch** - require pull request reviews
- [ ] **Sign Git commits** with GPG keys
- [ ] **Review manifest changes** carefully before merging
- [ ] **Never commit secrets** (API keys, credentials, private keys)
- [ ] **Audit access logs** periodically
- [ ] **Use GitHub Secret Scanning** (enabled by default)
- [ ] **Limit collaborator access** to necessary personnel only
- [ ] **Rotate credentials** if compromise suspected

### For Firmware Releases

- [ ] **Generate checksums programmatically** (not manually)
- [ ] **Double-check firmware URLs** before publishing manifest
- [ ] **Test on hardware** before releasing to stable channel
- [ ] **Document release contents** in GitHub Release notes
- [ ] **Monitor device update patterns** after release
- [ ] **Keep firmware binaries out of Git** (use `.gitignore`)

## Advanced Security: Code Signing

For enhanced security, implement firmware code signing:

### How It Works

1. **Generate Key Pair**: Create RSA or ECDSA signing key (keep private key secure)
2. **Sign Firmware**: Generate signature of firmware binary during build
3. **Add to Manifest**: Include signature in manifest JSON
4. **Verify on Device**: ESP32 verifies signature using public key before flashing

### Example Manifest Extension

```json
{
  "version": "1.2.3",
  "firmware_url": "https://github.com/SimmoM8/water-tank-level-sensor/releases/download/v1.2.3/firmware.bin",
  "sha256": "abcdef...",
  "signature": "base64_encoded_signature_here",
  "signature_algorithm": "RSA-SHA256",
  "size": 1048576,
  "release_notes": "...",
  "min_supported_version": "1.0.0",
  "release_date": "2026-02-05T10:30:00Z"
}
```

### Device Verification (Pseudocode)

```cpp
// Public key embedded in device firmware (or secure storage)
const char* public_key_pem = "-----BEGIN PUBLIC KEY-----...";

// After downloading firmware and verifying SHA-256
bool signature_valid = verify_signature(
    firmware_data,
    firmware_size,
    manifest.signature,
    public_key_pem,
    SIGNATURE_ALGORITHM_RSA_SHA256
);

if (!signature_valid) {
    log_error("Invalid firmware signature - aborting update");
    return;
}

// Proceed with OTA flash
esp_ota_write(...);
```

### Benefits of Code Signing

- ✅ Even if GitHub account is compromised, attacker can't publish valid firmware
- ✅ Stronger authentication than checksum alone
- ✅ Meets compliance requirements for some industries

### Considerations

- ⚠️ Adds complexity to build and release process
- ⚠️ Requires secure key management
- ⚠️ Key rotation is difficult (devices need public key update mechanism)

## ESP32 Secure Boot and Flash Encryption

For production deployments, consider ESP32 hardware security features:

### Secure Boot

- Ensures only cryptographically signed firmware can boot
- Protects against bootloader replacement attacks
- **Warning**: Irreversible once enabled - test thoroughly

```bash
# Enable during firmware build (PlatformIO)
board_build.secure_boot = yes
```

### Flash Encryption

- Encrypts firmware and data partitions on device
- Prevents firmware extraction via physical access
- **Warning**: Irreversible and can complicate development

```bash
# Enable during firmware build
board_build.flash_encryption = yes
```

### Recommendations

- Use **secure boot** for high-security applications
- Use **flash encryption** if devices are physically accessible to attackers
- Test extensively before enabling in production (cannot be reversed)
- Consider the impact on debugging and development workflow

## Vulnerability Reporting

If you discover a security vulnerability:

### Do NOT

- ❌ Open a public GitHub issue
- ❌ Discuss in public forums or social media
- ❌ Exploit the vulnerability

### Instead

1. **Email maintainers privately** at: [YOUR_SECURITY_EMAIL@example.com]
2. **Include**:
   - Description of the vulnerability
   - Steps to reproduce
   - Potential impact
   - Suggested fix (if any)
3. **Wait for response** before public disclosure
4. **Coordinate disclosure timeline** with maintainers

### Response Timeline

- **24 hours**: Acknowledgment of report
- **7 days**: Initial assessment and severity classification
- **30 days**: Fix developed, tested, and prepared for release
- **After fix deployed**: Public disclosure coordinated with reporter

## Security Update Policy

### Critical Vulnerabilities

- Hotfix releases within 24-48 hours
- Immediate update to stable channel
- Advisory published after fix deployed
- Devices notified via update mechanism

### Medium Vulnerabilities

- Fix included in next scheduled release
- Update deployed to dev channel first for testing
- Promoted to stable channel after validation

### Low-Risk Issues

- Fix scheduled for next major/minor release
- No emergency patching required

## Incident Response Plan

If a security incident occurs (e.g., malicious firmware released):

1. **Immediate Actions**
   - Revert manifest to last known-good version
   - Revoke GitHub personal access tokens (if compromised)
   - Disable affected GitHub accounts
   - Delete malicious GitHub Release

2. **Investigation**
   - Determine scope of compromise
   - Identify affected devices
   - Analyze malicious firmware (if released)

3. **Communication**
   - Notify device owners if possible
   - Publish security advisory
   - Document lessons learned

4. **Recovery**
   - Deploy fixed/clean firmware
   - Rotate credentials and keys
   - Enhance security controls
   - Monitor for recurrence

## Security Best Practices Summary

| Layer | Protection | Implementation |
|-------|-----------|----------------|
| **Transport** | HTTPS/TLS | GitHub CDN, ESP32 certificate validation |
| **Integrity** | SHA-256 checksums | Manifest verification before flashing |
| **Authentication** | Git signatures, 2FA | GitHub security features |
| **Authorization** | Access controls | Repository permissions, branch protection |
| **Confidentiality** | Flash encryption | ESP32 hardware feature (optional) |
| **Rollback** | Dual partitions | ESP32 OTA framework, `min_supported_version` |

## Additional Resources

- [ESP32 Secure Boot Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/security/secure-boot-v2.html)
- [ESP32 Flash Encryption Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/security/flash-encryption.html)
- [GitHub Security Best Practices](https://docs.github.com/en/code-security/getting-started/securing-your-organization)
- [OWASP IoT Security Guidance](https://owasp.org/www-project-internet-of-things/)

---

**Questions about security?** Contact repository maintainers or open a private security advisory.
