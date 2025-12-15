#ifndef CERTIFICATE_MANAGER_H
#define CERTIFICATE_MANAGER_H

#include <Arduino.h>
#include <Preferences.h>

// NVS namespace and keys for certificate storage
const char* const CERT_NVS_NAMESPACE = "certificates";
const char* const CERT_CA_KEY = "mqtt_ca_cert";
const char* const CERT_CLIENT_KEY = "mqtt_client_cert";
const char* const CERT_PRIV_KEY = "mqtt_client_key";
const char* const CERT_UPLOAD_TIME_KEY = "last_upload";

class CertificateManager {
public:
    CertificateManager();

    // Initialize NVS
    void begin(void);

    // Load certificates with fallback to compiled-in defaults (for WiFiClientSecure - PEM format)
    const char* getCACert(void);
    const char* getClientCert(void);
    const char* getClientKey(void);

    // Get certificates in DER format for SSLClient (BearSSL)
    // Returns dynamically allocated buffer - caller must free()
    uint8_t* getCACertDER(size_t* len);
    uint8_t* getClientCertDER(size_t* len);
    uint8_t* getClientKeyDER(size_t* len);

    // Save certificates to NVS
    bool saveCACert(const String& cert);
    bool saveClientCert(const String& cert);
    bool saveClientKey(const String& key);

    // Management
    void clearCertificates(void);
    bool hasCertificatesInNVS(void);
    String getCertificateSource(void);  // "NVS" or "Compiled-in"

    // Write-only security: Check presence without reading content
    bool hasCACertInNVS(void);
    bool hasClientCertInNVS(void);
    bool hasClientKeyInNVS(void);

    // Upload metadata tracking
    unsigned long getLastUploadTime(void);

private:
    Preferences prefs;

    // Buffers for NVS certificates (dynamically allocated)
    char* nvs_ca_cert;
    char* nvs_client_cert;
    char* nvs_client_key;

    // Flags to track certificate source
    bool using_nvs_ca;
    bool using_nvs_client;
    bool using_nvs_key;

    // Helper methods
    bool loadCertFromNVS(const char* key, char** buffer);
    void freeCertBuffers(void);
    uint8_t* convertPEMtoDER(const char* pem, size_t* outLen);
};

#endif // CERTIFICATE_MANAGER_H
