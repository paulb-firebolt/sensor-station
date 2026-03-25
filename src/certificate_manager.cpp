#include "certificate_manager.h"
#include "certs.h"
#include "log.h"

CertificateManager::CertificateManager()
    : nvs_ca_cert(nullptr),
      nvs_client_cert(nullptr),
      nvs_client_key(nullptr),
      using_nvs_ca(false),
      using_nvs_client(false),
      using_nvs_key(false) {}

void CertificateManager::begin(void) {
    LOG_I("[CertMgr] Initializing certificate manager\n");

    // Open NVS in read-write mode
    if (!prefs.begin(CERT_NVS_NAMESPACE, false)) {
        LOG_E("[CertMgr] ERROR: Failed to open NVS namespace\n");
        return;
    }

    // Try to load certificates from NVS
    using_nvs_ca = loadCertFromNVS(CERT_CA_KEY, &nvs_ca_cert);
    using_nvs_client = loadCertFromNVS(CERT_CLIENT_KEY, &nvs_client_cert);
    using_nvs_key = loadCertFromNVS(CERT_PRIV_KEY, &nvs_client_key);

    prefs.end();

    // Report what was loaded
    if (using_nvs_ca && using_nvs_client && using_nvs_key) {
        LOG_I("[CertMgr] All certificates loaded from NVS\n");
    } else if (!using_nvs_ca && !using_nvs_client && !using_nvs_key) {
        LOG_I("[CertMgr] No certificates in NVS, will use compiled-in defaults\n");
    } else {
        LOG_W("[CertMgr] WARNING: Partial certificates in NVS, using mixed sources\n");
        if (using_nvs_ca)
            LOG_I("[CertMgr]   CA: NVS\n");
        else
            LOG_I("[CertMgr]   CA: Compiled-in\n");
        if (using_nvs_client)
            LOG_I("[CertMgr]   Client Cert: NVS\n");
        else
            LOG_I("[CertMgr]   Client Cert: Compiled-in\n");
        if (using_nvs_key)
            LOG_I("[CertMgr]   Client Key: NVS\n");
        else
            LOG_I("[CertMgr]   Client Key: Compiled-in\n");
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
        LOG_E("[CertMgr] ERROR: Cannot save empty CA certificate\n");
        return false;
    }

    if (!prefs.begin(CERT_NVS_NAMESPACE, false)) {
        LOG_E("[CertMgr] ERROR: Failed to open NVS for writing\n");
        return false;
    }

    bool success = prefs.putString(CERT_CA_KEY, cert) > 0;

    // Track upload time
    if (success) {
        prefs.putULong(CERT_UPLOAD_TIME_KEY, millis());
    }

    prefs.end();

    if (success) {
        LOG_I("[CertMgr] CA certificate saved to NVS\n");
        if (nvs_ca_cert != nullptr) {
            free(nvs_ca_cert);
        }
        nvs_ca_cert = strdup(cert.c_str());
        using_nvs_ca = (nvs_ca_cert != nullptr);
    } else {
        LOG_E("[CertMgr] ERROR: Failed to save CA certificate\n");
    }

    return success;
}

bool CertificateManager::saveClientCert(const String& cert) {
    if (cert.length() == 0) {
        LOG_E("[CertMgr] ERROR: Cannot save empty client certificate\n");
        return false;
    }

    if (!prefs.begin(CERT_NVS_NAMESPACE, false)) {
        LOG_E("[CertMgr] ERROR: Failed to open NVS for writing\n");
        return false;
    }

    bool success = prefs.putString(CERT_CLIENT_KEY, cert) > 0;

    // Track upload time
    if (success) {
        prefs.putULong(CERT_UPLOAD_TIME_KEY, millis());
    }

    prefs.end();

    if (success) {
        LOG_I("[CertMgr] Client certificate saved to NVS\n");
        if (nvs_client_cert != nullptr) {
            free(nvs_client_cert);
        }
        nvs_client_cert = strdup(cert.c_str());
        using_nvs_client = (nvs_client_cert != nullptr);
    } else {
        LOG_E("[CertMgr] ERROR: Failed to save client certificate\n");
    }

    return success;
}

bool CertificateManager::saveClientKey(const String& key) {
    if (key.length() == 0) {
        LOG_E("[CertMgr] ERROR: Cannot save empty client key\n");
        return false;
    }

    if (!prefs.begin(CERT_NVS_NAMESPACE, false)) {
        LOG_E("[CertMgr] ERROR: Failed to open NVS for writing\n");
        return false;
    }

    bool success = prefs.putString(CERT_PRIV_KEY, key) > 0;

    // Track upload time
    if (success) {
        prefs.putULong(CERT_UPLOAD_TIME_KEY, millis());
    }

    prefs.end();

    if (success) {
        LOG_I("[CertMgr] Client key saved to NVS\n");
        if (nvs_client_key != nullptr) {
            free(nvs_client_key);
        }
        nvs_client_key = strdup(key.c_str());
        using_nvs_key = (nvs_client_key != nullptr);
    } else {
        LOG_E("[CertMgr] ERROR: Failed to save client key\n");
    }

    return success;
}

void CertificateManager::clearCertificates(void) {
    LOG_I("[CertMgr] Clearing all certificates from NVS\n");

    if (!prefs.begin(CERT_NVS_NAMESPACE, false)) {
        LOG_E("[CertMgr] ERROR: Failed to open NVS for clearing\n");
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

    LOG_I("[CertMgr] All certificates cleared, will use compiled-in defaults\n");
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
    if (!prefs.isKey(key)) {
        return false;
    }

    // getString(key, default) returns a String object — works even with large values
    String value = prefs.getString(key, "");
    if (value.length() == 0) {
        return false;
    }

    *buffer = strdup(value.c_str());
    if (*buffer == nullptr) {
        LOG_E("[CertMgr] ERROR: Failed to allocate memory for %s\n", key);
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
