#include "certificate_manager.h"
#include "certs.h"

CertificateManager::CertificateManager()
    : nvs_ca_cert(nullptr)
    , nvs_client_cert(nullptr)
    , nvs_client_key(nullptr)
    , using_nvs_ca(false)
    , using_nvs_client(false)
    , using_nvs_key(false) {
}

void CertificateManager::begin(void) {
    Serial.println("[CertMgr] Initializing certificate manager");

    // Open NVS in read-write mode
    if (!prefs.begin(CERT_NVS_NAMESPACE, false)) {
        Serial.println("[CertMgr] ERROR: Failed to open NVS namespace");
        return;
    }

    // Try to load certificates from NVS
    using_nvs_ca = loadCertFromNVS(CERT_CA_KEY, &nvs_ca_cert);
    using_nvs_client = loadCertFromNVS(CERT_CLIENT_KEY, &nvs_client_cert);
    using_nvs_key = loadCertFromNVS(CERT_PRIV_KEY, &nvs_client_key);

    prefs.end();

    // Report what was loaded
    if (using_nvs_ca && using_nvs_client && using_nvs_key) {
        Serial.println("[CertMgr] All certificates loaded from NVS");
    } else if (!using_nvs_ca && !using_nvs_client && !using_nvs_key) {
        Serial.println("[CertMgr] No certificates in NVS, will use compiled-in defaults");
    } else {
        Serial.println("[CertMgr] WARNING: Partial certificates in NVS, using mixed sources");
        if (using_nvs_ca) Serial.println("[CertMgr]   CA: NVS");
        else Serial.println("[CertMgr]   CA: Compiled-in");
        if (using_nvs_client) Serial.println("[CertMgr]   Client Cert: NVS");
        else Serial.println("[CertMgr]   Client Cert: Compiled-in");
        if (using_nvs_key) Serial.println("[CertMgr]   Client Key: NVS");
        else Serial.println("[CertMgr]   Client Key: Compiled-in");
    }
}

const char* CertificateManager::getCACert(void) {
    if (using_nvs_ca && nvs_ca_cert != nullptr) {
        return nvs_ca_cert;
    }
    return ca_cert;  // Fallback to compiled-in
}

const char* CertificateManager::getClientCert(void) {
    if (using_nvs_client && nvs_client_cert != nullptr) {
        return nvs_client_cert;
    }
    return client_cert;  // Fallback to compiled-in
}

const char* CertificateManager::getClientKey(void) {
    if (using_nvs_key && nvs_client_key != nullptr) {
        return nvs_client_key;
    }
    return client_key;  // Fallback to compiled-in
}

bool CertificateManager::saveCACert(const String& cert) {
    if (cert.length() == 0) {
        Serial.println("[CertMgr] ERROR: Cannot save empty CA certificate");
        return false;
    }

    if (!prefs.begin(CERT_NVS_NAMESPACE, false)) {
        Serial.println("[CertMgr] ERROR: Failed to open NVS for writing");
        return false;
    }

    bool success = prefs.putString(CERT_CA_KEY, cert) > 0;

    // Track upload time
    if (success) {
        prefs.putULong(CERT_UPLOAD_TIME_KEY, millis());
    }

    prefs.end();

    if (success) {
        Serial.println("[CertMgr] CA certificate saved to NVS");
        // Reload from NVS
        if (nvs_ca_cert != nullptr) {
            free(nvs_ca_cert);
            nvs_ca_cert = nullptr;
        }
        prefs.begin(CERT_NVS_NAMESPACE, true);  // Read-only
        using_nvs_ca = loadCertFromNVS(CERT_CA_KEY, &nvs_ca_cert);
        prefs.end();
    } else {
        Serial.println("[CertMgr] ERROR: Failed to save CA certificate");
    }

    return success;
}

bool CertificateManager::saveClientCert(const String& cert) {
    if (cert.length() == 0) {
        Serial.println("[CertMgr] ERROR: Cannot save empty client certificate");
        return false;
    }

    if (!prefs.begin(CERT_NVS_NAMESPACE, false)) {
        Serial.println("[CertMgr] ERROR: Failed to open NVS for writing");
        return false;
    }

    bool success = prefs.putString(CERT_CLIENT_KEY, cert) > 0;

    // Track upload time
    if (success) {
        prefs.putULong(CERT_UPLOAD_TIME_KEY, millis());
    }

    prefs.end();

    if (success) {
        Serial.println("[CertMgr] Client certificate saved to NVS");
        // Reload from NVS
        if (nvs_client_cert != nullptr) {
            free(nvs_client_cert);
            nvs_client_cert = nullptr;
        }
        prefs.begin(CERT_NVS_NAMESPACE, true);  // Read-only
        using_nvs_client = loadCertFromNVS(CERT_CLIENT_KEY, &nvs_client_cert);
        prefs.end();
    } else {
        Serial.println("[CertMgr] ERROR: Failed to save client certificate");
    }

    return success;
}

bool CertificateManager::saveClientKey(const String& key) {
    if (key.length() == 0) {
        Serial.println("[CertMgr] ERROR: Cannot save empty client key");
        return false;
    }

    if (!prefs.begin(CERT_NVS_NAMESPACE, false)) {
        Serial.println("[CertMgr] ERROR: Failed to open NVS for writing");
        return false;
    }

    bool success = prefs.putString(CERT_PRIV_KEY, key) > 0;

    // Track upload time
    if (success) {
        prefs.putULong(CERT_UPLOAD_TIME_KEY, millis());
    }

    prefs.end();

    if (success) {
        Serial.println("[CertMgr] Client key saved to NVS");
        // Reload from NVS
        if (nvs_client_key != nullptr) {
            free(nvs_client_key);
            nvs_client_key = nullptr;
        }
        prefs.begin(CERT_NVS_NAMESPACE, true);  // Read-only
        using_nvs_key = loadCertFromNVS(CERT_PRIV_KEY, &nvs_client_key);
        prefs.end();
    } else {
        Serial.println("[CertMgr] ERROR: Failed to save client key");
    }

    return success;
}

void CertificateManager::clearCertificates(void) {
    Serial.println("[CertMgr] Clearing all certificates from NVS");

    if (!prefs.begin(CERT_NVS_NAMESPACE, false)) {
        Serial.println("[CertMgr] ERROR: Failed to open NVS for clearing");
        return;
    }

    prefs.remove(CERT_CA_KEY);
    prefs.remove(CERT_CLIENT_KEY);
    prefs.remove(CERT_PRIV_KEY);
    prefs.remove(CERT_UPLOAD_TIME_KEY);  // Clear upload time too
    prefs.end();

    // Free memory and reset flags
    freeCertBuffers();
    using_nvs_ca = false;
    using_nvs_client = false;
    using_nvs_key = false;

    Serial.println("[CertMgr] All certificates cleared, will use compiled-in defaults");
}

bool CertificateManager::hasCertificatesInNVS(void) {
    return using_nvs_ca || using_nvs_client || using_nvs_key;
}

String CertificateManager::getCertificateSource(void) {
    if (using_nvs_ca && using_nvs_client && using_nvs_key) {
        return "NVS Storage";
    } else if (!using_nvs_ca && !using_nvs_client && !using_nvs_key) {
        return "Compiled-in";
    } else {
        return "Mixed (NVS + Compiled-in)";
    }
}

// Write-only security: Check presence without reading content
bool CertificateManager::hasCACertInNVS(void) {
    if (!prefs.begin(CERT_NVS_NAMESPACE, true)) {  // Read-only
        return false;
    }
    bool exists = prefs.isKey(CERT_CA_KEY);
    prefs.end();
    return exists;
}

bool CertificateManager::hasClientCertInNVS(void) {
    if (!prefs.begin(CERT_NVS_NAMESPACE, true)) {  // Read-only
        return false;
    }
    bool exists = prefs.isKey(CERT_CLIENT_KEY);
    prefs.end();
    return exists;
}

bool CertificateManager::hasClientKeyInNVS(void) {
    if (!prefs.begin(CERT_NVS_NAMESPACE, true)) {  // Read-only
        return false;
    }
    bool exists = prefs.isKey(CERT_PRIV_KEY);
    prefs.end();
    return exists;
}

// Upload metadata tracking
unsigned long CertificateManager::getLastUploadTime(void) {
    if (!prefs.begin(CERT_NVS_NAMESPACE, true)) {  // Read-only
        return 0;
    }
    unsigned long uploadTime = prefs.getULong(CERT_UPLOAD_TIME_KEY, 0);
    prefs.end();
    return uploadTime;
}

bool CertificateManager::loadCertFromNVS(const char* key, char** buffer) {
    // Check if key exists
    if (!prefs.isKey(key)) {
        return false;
    }

    // Get size
    size_t size = prefs.getString(key, nullptr, 0);
    if (size == 0) {
        return false;
    }

    // Allocate buffer
    *buffer = (char*)malloc(size);
    if (*buffer == nullptr) {
        Serial.print("[CertMgr] ERROR: Failed to allocate memory for ");
        Serial.println(key);
        return false;
    }

    // Load certificate
    size_t loaded = prefs.getString(key, *buffer, size);
    if (loaded == 0) {
        free(*buffer);
        *buffer = nullptr;
        return false;
    }

    return true;
}

void CertificateManager::freeCertBuffers(void) {
    if (nvs_ca_cert != nullptr) {
        free(nvs_ca_cert);
        nvs_ca_cert = nullptr;
    }
    if (nvs_client_cert != nullptr) {
        free(nvs_client_cert);
        nvs_client_cert = nullptr;
    }
    if (nvs_client_key != nullptr) {
        free(nvs_client_key);
        nvs_client_key = nullptr;
    }
}

// Base64 decoding table
static const uint8_t base64_decode_table[256] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x3E, 0xFF, 0xFF, 0xFF, 0x3F,
    0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0xFF, 0xFF, 0xFF, 0x00, 0xFF, 0xFF,
    0xFF, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E,
    0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28,
    0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F, 0x30, 0x31, 0x32, 0x33, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};

// Convert PEM to DER format (strip headers/footers and decode base64)
uint8_t* CertificateManager::convertPEMtoDER(const char* pem, size_t* outLen) {
    if (pem == nullptr || outLen == nullptr) {
        Serial.println("[CertMgr] ERROR: Invalid parameters to convertPEMtoDER");
        return nullptr;
    }

    // Find BEGIN marker
    const char* begin = strstr(pem, "-----BEGIN");
    if (begin == nullptr) {
        Serial.println("[CertMgr] ERROR: No BEGIN marker found in PEM");
        return nullptr;
    }

    // Skip to end of BEGIN line
    const char* dataStart = strchr(begin, '\n');
    if (dataStart == nullptr) {
        Serial.println("[CertMgr] ERROR: Malformed PEM BEGIN line");
        return nullptr;
    }
    dataStart++; // Skip newline

    // Find END marker
    const char* end = strstr(dataStart, "-----END");
    if (end == nullptr) {
        Serial.println("[CertMgr] ERROR: No END marker found in PEM");
        return nullptr;
    }

    // Calculate base64 data length (excluding newlines and whitespace)
    size_t base64Len = 0;
    for (const char* p = dataStart; p < end; p++) {
        if (*p != '\n' && *p != '\r' && *p != ' ' && *p != '\t') {
            base64Len++;
        }
    }

    // Allocate buffer for base64 data (without newlines)
    char* base64Data = (char*)malloc(base64Len + 1);
    if (base64Data == nullptr) {
        Serial.println("[CertMgr] ERROR: Failed to allocate base64 buffer");
        return nullptr;
    }

    // Copy base64 data, skipping newlines
    size_t j = 0;
    for (const char* p = dataStart; p < end; p++) {
        if (*p != '\n' && *p != '\r' && *p != ' ' && *p != '\t') {
            base64Data[j++] = *p;
        }
    }
    base64Data[j] = '\0';

    // Calculate DER output size (3 bytes per 4 base64 chars)
    size_t derLen = (base64Len * 3) / 4;

    // Handle padding
    if (base64Len > 0 && base64Data[base64Len - 1] == '=') derLen--;
    if (base64Len > 1 && base64Data[base64Len - 2] == '=') derLen--;

    // Allocate DER buffer
    uint8_t* derData = (uint8_t*)malloc(derLen);
    if (derData == nullptr) {
        Serial.println("[CertMgr] ERROR: Failed to allocate DER buffer");
        free(base64Data);
        return nullptr;
    }

    // Decode base64 to DER
    size_t derIdx = 0;
    uint32_t accumulator = 0;
    int bitsAccumulated = 0;

    for (size_t i = 0; i < base64Len; i++) {
        char c = base64Data[i];
        if (c == '=') break; // Padding

        uint8_t val = base64_decode_table[(uint8_t)c];
        if (val == 0xFF) {
            Serial.print("[CertMgr] ERROR: Invalid base64 character: ");
            Serial.println(c);
            free(base64Data);
            free(derData);
            return nullptr;
        }

        accumulator = (accumulator << 6) | val;
        bitsAccumulated += 6;

        if (bitsAccumulated >= 8) {
            bitsAccumulated -= 8;
            if (derIdx < derLen) {
                derData[derIdx++] = (accumulator >> bitsAccumulated) & 0xFF;
            }
        }
    }

    free(base64Data);
    *outLen = derLen;

    Serial.print("[CertMgr] Converted PEM to DER: ");
    Serial.print(derLen);
    Serial.println(" bytes");

    return derData;
}

// Get certificates in DER format for SSLClient
uint8_t* CertificateManager::getCACertDER(size_t* len) {
    const char* pem = getCACert();
    return convertPEMtoDER(pem, len);
}

uint8_t* CertificateManager::getClientCertDER(size_t* len) {
    const char* pem = getClientCert();
    return convertPEMtoDER(pem, len);
}

uint8_t* CertificateManager::getClientKeyDER(size_t* len) {
    const char* pem = getClientKey();
    return convertPEMtoDER(pem, len);
}
