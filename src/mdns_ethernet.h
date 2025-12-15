#ifndef MDNS_ETHERNET_H
#define MDNS_ETHERNET_H

#include <Arduino.h>
#include <Ethernet.h>
#include <EthernetUdp.h>

// mDNS constants
const uint16_t MDNS_PORT = 5353;
const IPAddress MDNS_MULTICAST_ADDR(224, 0, 0, 251);
const unsigned long MDNS_ANNOUNCE_INTERVAL = 120000; // 2 minutes

class MDNSEthernet {
public:
    MDNSEthernet();
    bool begin(const char* hostname, IPAddress localIP);
    void update();
    void announce();
    void addService(const char* service, const char* proto, uint16_t port);

private:
    EthernetUDP udp;
    String hostname;
    IPAddress localIP;
    unsigned long lastAnnounce;
    String serviceName;
    String serviceProto;
    uint16_t servicePort;
    bool hasService;

    void sendMDNSResponse(IPAddress destIP, uint16_t destPort, uint16_t transactionID = 0);
    void sendServiceAnnouncement();
    void processQuery();
    bool parseQuery(uint8_t* buffer, int len, uint16_t& transactionID);
};

#endif // MDNS_ETHERNET_H
