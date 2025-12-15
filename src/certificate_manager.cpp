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
