#include "mdns_ethernet.h"

MDNSEthernet::MDNSEthernet() : lastAnnounce(0), servicePort(0), hasService(false) {
}

bool MDNSEthernet::begin(const char* host, IPAddress ip) {
    hostname = String(host);
    localIP = ip;

    // Start UDP on mDNS port
    if (udp.beginMulticast(MDNS_MULTICAST_ADDR, MDNS_PORT) == 0) {
        return false;
    }

    // Send initial announcement
    delay(100);
    announce();
    delay(100);
    announce(); // Send twice for reliability

    lastAnnounce = millis();
    return true;
}

void MDNSEthernet::update() {
    // Check for incoming mDNS queries
    processQuery();

    // Periodic announcements
    unsigned long currentMillis = millis();
    if (currentMillis - lastAnnounce >= MDNS_ANNOUNCE_INTERVAL) {
        announce();
        lastAnnounce = currentMillis;
    }
}

void MDNSEthernet::announce() {
    sendMDNSResponse(MDNS_MULTICAST_ADDR, MDNS_PORT, 0);
    if (hasService) {
        sendServiceAnnouncement();
    }
}

void MDNSEthernet::addService(const char* service, const char* proto, uint16_t port) {
    serviceName = String(service);
    serviceProto = String(proto);
    servicePort = port;
    hasService = true;

    // Send initial service announcement
    delay(50);
    sendServiceAnnouncement();
}

void MDNSEthernet::sendServiceAnnouncement() {
    uint8_t buffer[512];
    int pos = 0;

    // Transaction ID: 0
    buffer[pos++] = 0x00;
    buffer[pos++] = 0x00;

    // Flags: Response, Authoritative Answer
    buffer[pos++] = 0x84;
    buffer[pos++] = 0x00;

    // Questions: 0
    buffer[pos++] = 0x00;
    buffer[pos++] = 0x00;

    // Answer RRs: 1 (PTR record)
    buffer[pos++] = 0x00;
    buffer[pos++] = 0x01;

    // Authority RRs: 0
    buffer[pos++] = 0x00;
    buffer[pos++] = 0x00;

    // Additional RRs: 2 (SRV + TXT)
    buffer[pos++] = 0x00;
    buffer[pos++] = 0x02;

    // PTR Record: _service._proto.local -> hostname._service._proto.local
    String serviceType = serviceName + "." + serviceProto + ".local";

    // Encode service type as DNS labels
    int labelStart = 0;
    for (int i = 0; i <= serviceType.length(); i++) {
        if (i == serviceType.length() || serviceType[i] == '.') {
            int labelLen = i - labelStart;
            if (labelLen > 0 && labelLen < 64) {
                buffer[pos++] = labelLen;
                for (int j = 0; j < labelLen; j++) {
                    buffer[pos++] = serviceType[labelStart + j];
                }
            }
            labelStart = i + 1;
        }
    }
    buffer[pos++] = 0x00; // End of name

    // Type: PTR (0x000C)
    buffer[pos++] = 0x00;
    buffer[pos++] = 0x0C;

    // Class: IN (0x0001)
    buffer[pos++] = 0x00;
    buffer[pos++] = 0x01;

    // TTL: 120 seconds
    buffer[pos++] = 0x00;
    buffer[pos++] = 0x00;
    buffer[pos++] = 0x00;
    buffer[pos++] = 0x78;

    // Data length placeholder
    int dataLenPos = pos;
    buffer[pos++] = 0x00;
    buffer[pos++] = 0x00;

    int dataStartPos = pos;

    // PTR data: hostname.serviceType
    String instanceName = hostname + "." + serviceType;
    labelStart = 0;
    for (int i = 0; i <= instanceName.length(); i++) {
        if (i == instanceName.length() || instanceName[i] == '.') {
            int labelLen = i - labelStart;
            if (labelLen > 0 && labelLen < 64) {
                buffer[pos++] = labelLen;
                for (int j = 0; j < labelLen; j++) {
                    buffer[pos++] = instanceName[labelStart + j];
                }
            }
            labelStart = i + 1;
        }
    }
    buffer[pos++] = 0x00; // End of name

    // Update data length
    int dataLen = pos - dataStartPos;
    buffer[dataLenPos] = (dataLen >> 8) & 0xFF;
    buffer[dataLenPos + 1] = dataLen & 0xFF;

    // SRV Record
    labelStart = 0;
    for (int i = 0; i <= instanceName.length(); i++) {
        if (i == instanceName.length() || instanceName[i] == '.') {
            int labelLen = i - labelStart;
            if (labelLen > 0 && labelLen < 64) {
                buffer[pos++] = labelLen;
                for (int j = 0; j < labelLen; j++) {
                    buffer[pos++] = instanceName[labelStart + j];
                }
            }
            labelStart = i + 1;
        }
    }
    buffer[pos++] = 0x00; // End of name

    // Type: SRV (0x0021)
    buffer[pos++] = 0x00;
    buffer[pos++] = 0x21;

    // Class: IN with cache-flush
    buffer[pos++] = 0x80;
    buffer[pos++] = 0x01;

    // TTL: 120 seconds
    buffer[pos++] = 0x00;
    buffer[pos++] = 0x00;
    buffer[pos++] = 0x00;
    buffer[pos++] = 0x78;

    // Data length placeholder
    dataLenPos = pos;
    buffer[pos++] = 0x00;
    buffer[pos++] = 0x00;

    dataStartPos = pos;

    // Priority: 0
    buffer[pos++] = 0x00;
    buffer[pos++] = 0x00;

    // Weight: 0
    buffer[pos++] = 0x00;
    buffer[pos++] = 0x00;

    // Port
    buffer[pos++] = (servicePort >> 8) & 0xFF;
    buffer[pos++] = servicePort & 0xFF;

    // Target: hostname.local
    String target = hostname + ".local";
    labelStart = 0;
    for (int i = 0; i <= target.length(); i++) {
        if (i == target.length() || target[i] == '.') {
            int labelLen = i - labelStart;
            if (labelLen > 0 && labelLen < 64) {
                buffer[pos++] = labelLen;
                for (int j = 0; j < labelLen; j++) {
                    buffer[pos++] = target[labelStart + j];
                }
            }
            labelStart = i + 1;
        }
    }
    buffer[pos++] = 0x00; // End of name

    // Update data length
    dataLen = pos - dataStartPos;
    buffer[dataLenPos] = (dataLen >> 8) & 0xFF;
    buffer[dataLenPos + 1] = dataLen & 0xFF;

    // TXT Record (empty)
    labelStart = 0;
    for (int i = 0; i <= instanceName.length(); i++) {
        if (i == instanceName.length() || instanceName[i] == '.') {
            int labelLen = i - labelStart;
            if (labelLen > 0 && labelLen < 64) {
                buffer[pos++] = labelLen;
                for (int j = 0; j < labelLen; j++) {
                    buffer[pos++] = instanceName[labelStart + j];
                }
            }
            labelStart = i + 1;
        }
    }
    buffer[pos++] = 0x00; // End of name

    // Type: TXT (0x0010)
    buffer[pos++] = 0x00;
    buffer[pos++] = 0x10;

    // Class: IN with cache-flush
    buffer[pos++] = 0x80;
    buffer[pos++] = 0x01;

    // TTL: 120 seconds
    buffer[pos++] = 0x00;
    buffer[pos++] = 0x00;
    buffer[pos++] = 0x00;
    buffer[pos++] = 0x78;

    // Data length: 1 (empty TXT)
    buffer[pos++] = 0x00;
    buffer[pos++] = 0x01;

    // Empty TXT record
    buffer[pos++] = 0x00;

    // Send the packet
    udp.beginPacket(MDNS_MULTICAST_ADDR, MDNS_PORT);
    udp.write(buffer, pos);
    udp.endPacket();
}

void MDNSEthernet::processQuery() {
    int packetSize = udp.parsePacket();
    if (packetSize > 0) {
        uint8_t buffer[512];
        int len = udp.read(buffer, sizeof(buffer));

        if (len > 12) { // Minimum mDNS header size
            uint16_t transactionID = 0;
            if (parseQuery(buffer, len, transactionID)) {
                // Respond to query
                sendMDNSResponse(udp.remoteIP(), udp.remotePort(), transactionID);
            }
        }
    }
}

bool MDNSEthernet::parseQuery(uint8_t* buffer, int len, uint16_t& transactionID) {
    // Parse mDNS query header
    transactionID = (buffer[0] << 8) | buffer[1];
    uint16_t flags = (buffer[2] << 8) | buffer[3];
    uint16_t questions = (buffer[4] << 8) | buffer[5];

    // Check if it's a query (QR bit = 0)
    if ((flags & 0x8000) != 0) {
        return false; // It's a response, not a query
    }

    if (questions == 0) {
        return false; // No questions
    }

    // Parse question section
    int pos = 12; // After header
    String queryName = "";

    while (pos < len && buffer[pos] != 0) {
        int labelLen = buffer[pos];
        if (labelLen == 0 || labelLen > 63 || pos + labelLen >= len) {
            break;
        }

        if (!queryName.isEmpty()) {
            queryName += ".";
        }

        for (int i = 0; i < labelLen; i++) {
            queryName += (char)buffer[pos + 1 + i];
        }

        pos += labelLen + 1;
    }

    // Convert hostname to lowercase for comparison
    String hostnameLocal = hostname;
    hostnameLocal.toLowerCase();
    queryName.toLowerCase();

    // Check if query matches our hostname
    String fullHostname = hostnameLocal + ".local";
    return (queryName == fullHostname || queryName == hostnameLocal);
}

void MDNSEthernet::sendMDNSResponse(IPAddress destIP, uint16_t destPort, uint16_t transactionID) {
    uint8_t buffer[512];
    int pos = 0;

    // Transaction ID
    buffer[pos++] = (transactionID >> 8) & 0xFF;
    buffer[pos++] = transactionID & 0xFF;

    // Flags: Response, Authoritative Answer
    buffer[pos++] = 0x84;
    buffer[pos++] = 0x00;

    // Questions: 0
    buffer[pos++] = 0x00;
    buffer[pos++] = 0x00;

    // Answer RRs: 1
    buffer[pos++] = 0x00;
    buffer[pos++] = 0x01;

    // Authority RRs: 0
    buffer[pos++] = 0x00;
    buffer[pos++] = 0x00;

    // Additional RRs: 0
    buffer[pos++] = 0x00;
    buffer[pos++] = 0x00;

    // Answer section - hostname
    String fullHostname = hostname + ".local";

    // Encode hostname as DNS labels
    int labelStart = 0;
    int hostnameLen = fullHostname.length();

    for (int i = 0; i <= hostnameLen; i++) {
        if (i == hostnameLen || fullHostname[i] == '.') {
            int labelLen = i - labelStart;
            if (labelLen > 0 && labelLen < 64) {
                buffer[pos++] = labelLen;
                for (int j = 0; j < labelLen; j++) {
                    buffer[pos++] = fullHostname[labelStart + j];
                }
            }
            labelStart = i + 1;
        }
    }

    buffer[pos++] = 0x00; // End of name

    // Type: A (0x0001)
    buffer[pos++] = 0x00;
    buffer[pos++] = 0x01;

    // Class: IN (0x0001) with cache-flush bit
    buffer[pos++] = 0x80;
    buffer[pos++] = 0x01;

    // TTL: 120 seconds
    buffer[pos++] = 0x00;
    buffer[pos++] = 0x00;
    buffer[pos++] = 0x00;
    buffer[pos++] = 0x78;

    // Data length: 4 (IPv4 address)
    buffer[pos++] = 0x00;
    buffer[pos++] = 0x04;

    // IP address
    buffer[pos++] = localIP[0];
    buffer[pos++] = localIP[1];
    buffer[pos++] = localIP[2];
    buffer[pos++] = localIP[3];

    // Send the packet
    udp.beginPacket(destIP, destPort);
    udp.write(buffer, pos);
    udp.endPacket();
}
