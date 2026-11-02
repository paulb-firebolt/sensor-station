---
title: Device Certificates and Provisioning
created: 2026-10-30T00:00:00Z
lastModified: 2026-11-02T00:00:00Z
---

**Status: draft for review.** This note proposes how base stations get a unique
identity at manufacture and use it to connect to MQTT, on ThingsBoard Community
Edition now and on AWS IoT Core if the platform changes. Nothing here is built yet.

## Summary

- Every base station gets its own X.509 client certificate at flash time. It
  generates its own private key, which never leaves the chip. No two stations share
  a key.
- Certificates are signed by an **intermediate CA** held by the flash station. The
  **root CA** stays offline and only signs intermediates.
- Stations connect over TLS with the certificate alone: no username, no password.
- The broker trusts the root and holds no per-device state. One ACL pattern
  confines each station to its own topics.
- The firmware speaks one MQTT protocol (our topics, our JSON contract). Platform
  differences are handled in configuration and on the cloud side, not by a
  protocol switch in the firmware.
- The same factory certificate works with Mosquitto + ThingsBoard and with AWS IoT
  Core, so moving platform is a configuration change, not a firmware release.

These certificates are for **base stations**, the devices that connect to MQTT.
Sub-1 GHz sensor nodes do not connect to MQTT and do not get certificates; they
are admitted by enrolment on their station (see
[MQTT command reference](../mqtt-command-reference.md), `accept_node`).

## Current state

**Legacy stations (ESP8266, in the field):** plain MQTT with no TLS, straight to
ThingsBoard CE's MQTT transport. The flashing process reads the device's built-in
ID and calls the ThingsBoard HTTP API to create the device. Credentials and data
cross the network unencrypted.

**New firmware (this repo):**

- MQTT over TLS with a client certificate, plus an optional username and password.
- The CA, client certificate and key come from NVS (namespace `certificates`),
  falling back to compiled-in defaults in `src/certs.h`. They can be uploaded
  through the web UI.
- Client ID and the device part of every topic are the station's hostname,
  `sensor-XXXXXXXX`, derived from its MAC address. The topic prefix is configurable
  in NVS (default `sensors/esp32`).

Two problems to fix before anything ships:

1. `src/certs.h` holds a client certificate **and its private key**, tracked in git.
   Any station without NVS certificates falls back to that shared identity.
2. The bench broker requires a username and password as well as the certificate.
   That is per-device state on the broker, and AWS IoT Core does not accept it.

## Certificate hierarchy

```text
Root CA (offline)
 └── Intermediate CA "flash station 1" (pathlen:0, valid 15 years, signs for 2)
      ├── sensor-1A2B3C4D   (station certificate)
      ├── sensor-5E6F7081
      └── ...
 └── Intermediate CA "flash station 2" (another site or line)
      └── ...
```

**Root CA**

- Created once, kept offline (encrypted media in a safe, with a backup copy held
  separately).
- Used only to issue and revoke intermediates.
- Lifetime 30+ years: it must outlive the last intermediate it signs, and each
  intermediate is valid for 15 years after it is issued.

**Intermediate CA, one per flash station or production line**

- Signs station certificates at flash time.
- `basicConstraints = CA:true, pathlen:0`, so it cannot create further CAs.
- Valid for 15 years, but used to sign stations for only 2. Then its key is
  destroyed and a new intermediate is issued from the root. Rotating limits how many
  stations any one intermediate covers.
- Destroying the key only stops new signing. The intermediate certificate is public,
  stays in each station's chain and on the broker, and is needed to verify its stations
  until it expires. The broker rejects a station whose chain contains an expired
  certificate.
- Key held in a hardware token (YubiKey PIV or similar) where practical, so it
  cannot be copied off the PC.

**Station certificate**

- Subject CN = the station's client ID (today `sensor-XXXXXXXX`; see open questions).
- `extendedKeyUsage = clientAuth`.
- Expires on the same date as the intermediate that signed it, so a station gets
  13–15 years depending on when in the 2-year signing window it was made. Every
  station from one intermediate expires on one date.
- The station does not check certificate dates (mbedTLS is built without
  `CONFIG_MBEDTLS_HAVE_TIME_DATE`), but the broker does check the station's chain.
- ECDSA P-256 keys: smaller and faster to handshake on the ESP32 than RSA-2048.

## Factory issuance

This replaces today's step "read the device ID, call the ThingsBoard API", at the
same point in the flashing process, over the same USB connection.

**Decided: the station generates its own key (option B), and the certificate is
issued over USB.** The private key never leaves the chip. Physical presence on the
flash station's USB port is what authenticates the request, so no enrolment service is
exposed on a network.

1. Flash the firmware.
2. Read the station's ID from the chip.
3. Serial command `csr`: the station generates an ECDSA P-256 key pair into NVS (if
   it has none) and returns a certificate signing request (CSR) with CN = its ID.
4. Check that the CSR's CN matches the ID read in step 2.
5. Sign the CSR with the intermediate CA.
6. Serial command `cert`: write into the station's NVS:
   - its certificate;
   - the intermediate certificate (the chain);
   - the broker CA, endpoint and port.
7. Record serial number, station ID, intermediate and time in the issuance log.
   The log is needed for revocation decisions.
8. Optionally create the device in the platform: the ThingsBoard HTTP API call as
   today, or nothing on AWS (see below).

The CA key must be readable only by the signing step, ideally held on a hardware token.

### Cost on the station

P-256 key generation and CSR creation use mbedTLS, which the firmware already carries
for TLS. Both run in well under a second on the ESP32-S3 and the ESP32-P4, once per
station. RSA-2048 key generation can take tens of seconds and is not used.

### Flash station

The flash station is a script around the existing flashing tools, not a service:

```text
esptool write-flash <firmware>                 (as today)
serial: csr          → CSR, CN = sensor-XXXXXXXX
check CN against the ID read from the chip
sign with the intermediate CA                  (openssl or step CLI, key on a token)
serial: cert <certificate + chain + broker settings>
append to the issuance log
optional: ThingsBoard API call                 (as today)
```

The certificate steps take about 5 s per station; plugging and unplugging dominates,
at roughly 20–30 s per station on one port. A powered USB hub with one script instance
per port handles a row of stations at a time. With 10 ports, 600 stations take about
an hour.

### Alternatives considered

- **Flash station generates the key (option A).** Simpler firmware, but the private key
  exists on the flash PC, which then has to be treated as sensitive. Rejected in favour
  of option B.
- **Enrolment over a closed bench network.** Stations on an isolated network send their
  CSR over HTTPS to an enrolment server at an address fixed in the firmware, verified
  against a CA pinned in the firmware. The server signs only IDs on the production list,
  once each, and returns the certificate in the response. This avoids cabling each
  station to USB, but needs the enrolment server, the pinned CA and the allowlist. Use it
  if stations must be issued certificates where USB handling is impractical.

## Storage on the station

- Certificate, chain and key in NVS. Not compiled into the firmware.
- Enable **flash encryption** (and secure boot) on production units, so a station
  taken off a wall cannot have its key read out of flash.
- Remove the compiled-in client certificate and key from `src/certs.h`. A station
  with no provisioned identity should fail to connect, not borrow a shared one.
- Keep the identity (key, certificate, chain) out of reach of factory reset. Today a
  factory reset clears all of NVS, which would leave a station with no identity until
  it goes back to a flash station. Store the identity in its own NVS namespace or
  partition that factory reset does not erase.
- To check: the ESP32-P4 may be able to hold the key in eFuse through its ECDSA
  peripheral, so software can never read it. The ESP32-S3's signing peripheral is
  RSA-only, as far as is known, so on the S3 flash encryption is the protection.

## Broker: Mosquitto for ThingsBoard CE

ThingsBoard CE's built-in MQTT transport only accepts ThingsBoard's own device API
topics (`v1/devices/me/...`, `v1/gateway/...`) with per-device credentials held in
ThingsBoard. It cannot carry our topics, and other services cannot subscribe to it.
So this design runs our own broker beside ThingsBoard:

```text
stations ──TLS──▶ Mosquitto ──▶ ThingsBoard IoT Gateway (MQTT connector) ──▶ ThingsBoard CE
```

Mosquitto settings:

```conf
listener 8883
cafile    /etc/mosquitto/ca/root-ca.pem
certfile  /etc/mosquitto/certs/broker.pem
keyfile   /etc/mosquitto/certs/broker.key
require_certificate true
use_identity_as_username true
crlfile   /etc/mosquitto/ca/crl.pem
acl_file  /etc/mosquitto/acl
```

ACL file:

```conf
# Each station may only use its own topics; %u is the certificate CN
pattern readwrite sensors/esp32/%u/#

# The IoT Gateway's own certificate
user tb-gateway
topic readwrite sensors/#
```

Stations send their certificate plus the intermediate. Mosquitto checks the chain
back to the root.

Adding a station needs **no broker change**. The broker holds the root, the CRL and
one ACL pattern, regardless of fleet size.

The IoT Gateway creates a ThingsBoard device for each new station or sensor on its
first message. Our `cc1312/nodes` messages are batches, so the gateway needs a small
custom converter. It splits each batch into per-sensor telemetry and turns
ThingsBoard RPC into publishes on `…/command`. That converter should follow
`v3-data-contract.asyncapi.yaml`.

## AWS IoT Core

IoT Core is the broker, so Mosquitto and the gateway drop out. Stations keep the
same topics and the same factory certificate.

- Register the intermediate CA(s) with AWS IoT, with a **just-in-time provisioning
  (JITP)** template that, for each new certificate:
  - creates a thing named after its CN;
  - attaches a policy allowing `sensors/esp32/${iot:Connection.Thing.ThingName}/*`.
- A station's first connection is refused while AWS registers it. The station's
  normal reconnect succeeds a few seconds later. No registration code is needed in
  the firmware.
- Routing to storage or processing is done with IoT rules on our topics.

Constraints the firmware must meet, all configuration:

| Setting           | Mosquitto             | AWS IoT Core                                        |
| ----------------- | --------------------- | --------------------------------------------------- |
| Endpoint          | our broker            | account IoT endpoint                                |
| Server CA         | our root              | Amazon Root CA                                      |
| Client ID         | certificate CN        | certificate CN (= thing name)                       |
| Username/password | none                  | none                                                |
| Chain sent        | device + intermediate | device + intermediate (check against AWS JITP docs) |
| QoS               | 0/1                   | 0/1 only                                            |

## Revocation

- **Single station** (stolen, retired or faulty): add its certificate serial to the
  CRL. Mosquitto reloads the CRL; in AWS, deactivate that certificate.
- **Intermediate compromised:**
  - Use the issuance log to decide what to revoke. If the time of compromise is
    known, revoke only the certificates issued after it.
  - Revoking the intermediate itself distrusts every station it ever signed,
    including healthy ones in the field.
  - Regular intermediate rotation keeps that blast radius small.
- **Root:** never online, so compromise should not arise. Losing it means
  re-issuing everything, which is why it has a backup.

## Firmware changes

1. Connect with the certificate only, with no username or password.
2. Make the endpoint, port, server CA, client ID, topic prefix and certificate chain
   all NVS configuration, provisioned at flash time and changeable by OTA or the
   config page.
3. Send the intermediate certificate with the station certificate.
4. Remove the compiled-in client certificate and key from `src/certs.h`, and drop
   the shared-identity fallback.
5. Keep reconnect with backoff, which already exists; JITP relies on it.
6. Serial commands for the flash station: `csr` (generate the P-256 key if absent,
   return a CSR) and `cert` (store the certificate, chain and broker settings).
7. Keep the identity across factory reset (see Storage on the station).

## Production changes

1. Create the root CA and the first intermediate.
2. Add steps 2–8 of the factory issuance to the flashing script, with one instance
   per port on a powered USB hub.
3. Keep or drop the ThingsBoard API call: the gateway can create devices on first
   message, but pre-creating them lets staff attach customer and site details.
4. Keep the issuance log somewhere backed up.

## Open questions

- **Station ID.** Today the client ID is derived from the MAC (`sensor-XXXXXXXX`).
  Should the certificate CN use that, or the ID the production process already
  reads and registers in ThingsBoard? They should be the same thing.
- **Who flashes.** If stations will ever be flashed by a contract manufacturer,
  they get their own intermediate and a copy of the flash station script, or
  certificates are issued by closed-network enrolment (see Alternatives
  considered).
- **Expiry on AWS.** Check how AWS IoT Core treats an expired device or registered CA
  certificate. Mosquitto rejects them.
- **Gateway ownership.** The ThingsBoard IoT Gateway and its converter become a
  service someone runs, monitors and updates.
- **Legacy fleet.** Existing ESP8266 stations stay on plain MQTT to ThingsBoard's
  own transport. Both paths can run side by side during migration.
