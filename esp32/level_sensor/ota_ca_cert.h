#pragma once
#include <WiFiClientSecure.h>

// Prefer the ESP-IDF/Arduino built-in certificate bundle when available.
// This keeps trust roots current across certificate rotations.
#if defined(ARDUINO_ARCH_ESP32) && defined(CONFIG_MBEDTLS_CERTIFICATE_BUNDLE)
#define OTA_HAS_CERT_BUNDLE 1
extern "C"
{
    extern const uint8_t _binary_x509_crt_bundle_start[] asm("_binary_x509_crt_bundle_start");
    extern const uint8_t _binary_x509_crt_bundle_end[] asm("_binary_x509_crt_bundle_end");
}
#else
#define OTA_HAS_CERT_BUNDLE 0
#endif

// Fallback root CA (complete PEM): DigiCert Global Root G2.
// Used by GitHub domains when certificate bundle support is unavailable.
static const char OTA_FALLBACK_GITHUB_CA[] = R"EOF(
-----BEGIN CERTIFICATE-----
MIIDjjCCAnagAwIBAgIQAzrx5qcRqaC7KGSxHQn65TANBgkqhkiG9w0BAQsFADBh
MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3
d3cuZGlnaWNlcnQuY29tMSAwHgYDVQQDExdEaWdpQ2VydCBHbG9iYWwgUm9vdCBH
MjAeFw0xMzA4MDExMjAwMDBaFw0zODAxMTUxMjAwMDBaMGExCzAJBgNVBAYTAlVT
MRUwEwYDVQQKEwxEaWdpQ2VydCBJbmMxGTAXBgNVBAsTEHd3dy5kaWdpY2VydC5j
b20xIDAeBgNVBAMTF0RpZ2lDZXJ0IEdsb2JhbCBSb290IEcyMIIBIjANBgkqhkiG
9w0BAQEFAAOCAQ8AMIIBCgKCAQEAuzfNNNx7a8myaJCtSnX/RrohCgiN9RlUyfuI
2/Ou8jqJkTx65qsGGmvPrC3oXgkkRLpimn7Wo6h+4FR1IAWsULecYxpsMNzaHxmx
1x7e/dfgy5SDN67sH0NO3Xss0r0upS/kqbitOtSZpLYl6ZtrAGCSYP9PIUkY92eQ
q2EGnI/yuum06ZIya7XzV+hdG82MHauVBJVJ8zUtluNJbd134/tJS7SsVQepj5Wz
tCO7TG1F8PapspUwtP1MVYwnSlcUfIKdzXOS0xZKBgyMUNGPHgm+F6HmIcr9g+UQ
vIOlCsRnKPZzFBQ9RnbDhxSJITRNrw9FDKZJobq7nMWxM4MphQIDAQABo0IwQDAP
BgNVHRMBAf8EBTADAQH/MA4GA1UdDwEB/wQEAwIBhjAdBgNVHQ4EFgQUTiJUIBiV
5uNu5g/6+rkS7QYXjzkwDQYJKoZIhvcNAQELBQADggEBAGBnKJRvDkhj6zHd6mcY
1Yl9PMWLSn/pvtsrF9+wX3N3KjITOYFnQoQj8kVnNeyIv/iPsGEMNKSuIEyExtv4
NeF22d+mQrvHRAiGfzZ0JFrabA0UWTW98kndth/Jsw1HKj2ZL7tcu7XUIOGZX1NG
Fdtom/DzMNU+MeKNhJ7jitralj41E6Vf8PlwUHBHQRFXGU7Aj64GxJUTFy8bJZ91
8rGOmaFvE7FBcf6IKshPECBV1/MUReXgRPTqh5Uykw7+U0b6LJ3/iyK5S9kJRaTe
pLiaWN0bfVKfjllDiIGknibVb63dDcY3fe0Dkhvld1927jyNxF1WW6LZZm6zNTfl
MrY=
-----END CERTIFICATE-----
)EOF";

enum class OtaCaMode : uint8_t
{
    CertBundle = 0,
    PinnedPem = 1,
};

#if OTA_HAS_CERT_BUNDLE
template <typename ClientT>
static inline auto ota_setCaBundle(ClientT &client, const uint8_t *bundle, size_t bundleSize, int)
    -> decltype(client.setCACertBundle(bundle, bundleSize), void())
{
    client.setCACertBundle(bundle, bundleSize);
}

template <typename ClientT>
static inline auto ota_setCaBundle(ClientT &client, const uint8_t *bundle, size_t, long)
    -> decltype(client.setCACertBundle(bundle), void())
{
    client.setCACertBundle(bundle);
}
#endif

static inline OtaCaMode ota_configureTlsClient(WiFiClientSecure &client)
{
    client.setTimeout(12000); // ms
#if OTA_HAS_CERT_BUNDLE
    const size_t bundleSize = (size_t)(_binary_x509_crt_bundle_end - _binary_x509_crt_bundle_start);
    if (bundleSize > 0)
    {
        ota_setCaBundle(client, _binary_x509_crt_bundle_start, bundleSize, 0);
        return OtaCaMode::CertBundle;
    }
    ota_setCaBundle(client, nullptr, 0, 0);
#endif
    client.setCACert(OTA_FALLBACK_GITHUB_CA);
    return OtaCaMode::PinnedPem;
}

static inline const char *ota_caModeName(OtaCaMode mode)
{
    switch (mode)
    {
    case OtaCaMode::CertBundle:
        return "bundle";
    case OtaCaMode::PinnedPem:
    default:
        return "pinned_pem";
    }
}
