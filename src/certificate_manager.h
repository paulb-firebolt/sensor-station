/**
 * @file certificate_manager.h
 * @brief Manages mTLS certificates (CA cert, client cert, client key) stored
 *        in NVS, with compiled-in defaults as fallback.
 */

#ifndef CERTIFICATE_MANAGER_H
#define CERTIFICATE_MANAGER_H

#include <Arduino.h>
#include <Preferences.h>

// NVS namespace and keys for certificate storage
const char* const CERT_NVS_NAMESPACE = "certificates";
const char* const CERT_CA_KEY = "mqtt_ca_cert";
const char* const CERT_CLIENT_KEY = "mqtt_clt_cert";  // max 15 chars for NVS
const char* const CERT_PRIV_KEY = "mqtt_client_key";
const char* const CERT_UPLOAD_TIME_KEY = "last_upload";

/**
 * @brief Manages mTLS certificates for MQTTS connections.
 *
 * Certificates are stored persistently in NVS and loaded into heap buffers
 * on startup. When NVS is empty the compiled-in default certificates are
 * used as a fallback, enabling out-of-box connectivity without provisioning.
 */
class CertificateManager {
public:
    CertificateManager();

    /**
     * @brief Loads certificates from NVS into heap buffers.
     *
     * Falls back to compiled-in defaults for any certificate not found in
     * NVS. Must be called before any getter is used.
     */
    void begin(void);

    /**
     * @brief Returns the PEM-encoded CA certificate.
     *
     * NVS storage takes priority over the compiled-in default. The returned
     * pointer is valid for the lifetime of this object.
     *
     * @return Null-terminated PEM string for the CA certificate.
     */
    const char* getCACert(void);

    /**
     * @brief Returns the PEM-encoded client certificate.
     *
     * NVS storage takes priority over the compiled-in default. The returned
     * pointer is valid for the lifetime of this object.
     *
     * @return Null-terminated PEM string for the client certificate.
     */
    const char* getClientCert(void);

    /**
     * @brief Returns the PEM-encoded client private key.
     *
     * NVS storage takes priority over the compiled-in default. The returned
     * pointer is valid for the lifetime of this object.
     *
     * @return Null-terminated PEM string for the client private key.
     */
    const char* getClientKey(void);

    /**
     * @brief Persists the CA certificate to NVS.
     *
     * @param cert PEM-encoded CA certificate string.
     * @return true if the NVS write succeeded, false on failure.
     */
    bool saveCACert(const String& cert);

    /**
     * @brief Persists the client certificate to NVS.
     *
     * @param cert PEM-encoded client certificate string.
     * @return true if the NVS write succeeded, false on failure.
     */
    bool saveClientCert(const String& cert);

    /**
     * @brief Persists the client private key to NVS.
     *
     * @param key PEM-encoded client private key string.
     * @return true if the NVS write succeeded, false on failure.
     */
    bool saveClientKey(const String& key);

    /**
     * @brief Removes all certificates from NVS and frees heap buffers.
     *
     * After this call, getters fall back to compiled-in defaults and
     * `hasCertificatesInNVS()` returns false.
     */
    void clearCertificates(void);

    /**
     * @brief Returns true if any certificate is stored in NVS.
     *
     * @return true when at least one of the CA cert, client cert, or client
     *         key has been saved to NVS.
     */
    bool hasCertificatesInNVS(void);

    /**
     * @brief Returns a string describing the active certificate source.
     *
     * Possible return values: "NVS Storage", "Compiled-in", or "Mixed"
     * (when some certs come from NVS and others from compiled-in defaults).
     *
     * @return Source description string.
     */
    String getCertificateSource(void);  // "NVS" or "Compiled-in"

    /**
     * @brief Returns true if a CA certificate is present in NVS.
     *
     * Checks for presence only — does not return the certificate content,
     * following the write-only security pattern.
     *
     * @return true if the CA certificate key exists in NVS.
     */
    bool hasCACertInNVS(void);

    /**
     * @brief Returns true if a client certificate is present in NVS.
     *
     * Checks for presence only — does not return the certificate content,
     * following the write-only security pattern.
     *
     * @return true if the client certificate key exists in NVS.
     */
    bool hasClientCertInNVS(void);

    /**
     * @brief Returns true if a client private key is present in NVS.
     *
     * Checks for presence only — does not return the key content,
     * following the write-only security pattern.
     *
     * @return true if the client private key exists in NVS.
     */
    bool hasClientKeyInNVS(void);

    /**
     * @brief Returns the millis() timestamp of the most recent NVS cert upload.
     *
     * @return Milliseconds since boot at the time of the last successful
     *         certificate save, or 0 if no upload has occurred.
     */
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
};

#endif  // CERTIFICATE_MANAGER_H
