---
title: CC1312R SPI Interaction — Function-Level Sequence Diagrams
created: 2026-03-25T00:00:00Z
updated: 2026-03-25T00:00:00Z
---

# CC1312R SPI Interaction — Function-Level Sequence Diagrams <!-- trunk-ignore(markdownlint/MD025) -->

This document maps every major interaction between the CC1312R coordinator firmware
(`rfEchoRx.c`) and the ESP32-P4 SPI driver (`cc1312_manager.h`) as Mermaid sequence
diagrams. Each step names the function responsible on each side.

The physical layer beneath all of these flows is the 5-wire SPI link:

```
ESP32-P4 G8  MOSI ──► CC1312R DIO11 SSI_RX
ESP32-P4 G9  MISO ◄── CC1312R DIO9  SSI_TX
ESP32-P4 G10 CLK  ──► CC1312R DIO10 SSI_CLK
ESP32-P4 G11 CS   ──► CC1312R DIO8  SSI_FSS  (active-low)
ESP32-P4 G12 DRDY ◄── CC1312R DIO12 GPIO out (active-low)
```

---

## 1 — Startup and First Heartbeat

Both sides initialise independently. The CC1312R starts its main loop and immediately
arms an SPI RX DMA transfer to listen for downlinks. The first heartbeat is sent 30
seconds after boot.

```mermaid
sequenceDiagram
    participant C as CC1312R (rfEchoRx.c)
    participant W as SPI Bus (wires)
    participant E as ESP32-P4 (cc1312_manager.h)

    Note over C: Boot — SPI_open(), GPIO_write(DRDY, 1)
    C->>C: startSpiRxTransfer()<br/>arms DMA RX transfer, spiTransferMode=1

    Note over E: setup() → cc1312.begin()
    E->>E: _spi.begin(CLK, MISO, MOSI)<br/>pinMode(CS, OUTPUT), CS=HIGH<br/>pinMode(DRDY, INPUT_PULLUP)<br/>_loadEnrolled() — restores NVS node list

    Note over C: 30 s timer fires (nextHeartbeatRat)
    C->>C: writeFrame(UART_MSG_HEARTBEAT, addr, fw_version)<br/>→ enqueueSpiFrame()
    C->>C: serviceSpiTransport()<br/>spiFrameQueueCount>0 → startSpiTxTransfer()
    C->>C: SPI_transfer(spiHandle, &spiTxTransaction)<br/>spiTransferMode=2
    C->>C: spiWaitForTxFifoPrimed()<br/>polls SSI_O_SR.TFE until FIFO not empty
    C->>C: usleep(100 µs)
    C->>W: DRDY → LOW  (frame is primed)

    Note over E: loop() → cc1312.update() — every iteration
    E->>E: digitalRead(DRDY)==HIGH? → _drdyArmed=true
    E->>E: _drdyArmed && digitalRead(DRDY)==LOW?<br/>→ _drdyArmed=false, _drdyTriggers++

    E->>W: CS → LOW
    E->>W: SPI clock: transfer(0x00) → reads START (0xAA)
    W-->>E: 0xAA
    E->>W: SPI clock: transfer(0x00) → reads LEN
    W-->>E: 0x0D  (13 bytes)
    E->>W: SPI clock: transfer ×14 → reads body + CRC
    W-->>E: [MSG_TYPE=0x06][addr×8][rssi][fw_major][fw_minor][fw_patch][CRC8]
    E->>W: CS → HIGH

    Note over C: CS deassert triggers spiTransferCallback()
    C->>C: spiTransferCallback()<br/>GPIO_write(DRDY, 1)  ← deassert<br/>dequeueSpiFrame() — retires frame<br/>spiFrameQueueCount==0 → armSpiRxTransfer(0)

    Note over C: Next serviceSpiTransport() call
    C->>C: serviceSpiTransport()<br/>spiTransferComplete==1, mode was TX<br/>spiTransferMode=0<br/>spiFrameQueueCount==0 → startSpiRxTransfer()
    C->>C: armSpiRxTransfer(1) — DMA RX re-armed

    E->>E: _parseByte() × (2 + LEN + 1)<br/>→ _dispatchFrame(0x06, payload, len)
    E->>E: _lastHeartbeat = millis()<br/>_coordinatorAddr = addr<br/>_coordinatorVersion = {major, minor, patch}<br/>Serial.printf("[CC1312] Coordinator heartbeat...")
```

---

## 2 — Node-List Request and Sync

The CC1312R asks the ESP32-P4 for its enrolled node list on startup (and every 10 s
while the list is empty, or every 300 s as a keepalive once the list is populated).

```mermaid
sequenceDiagram
    participant RF as RF Nodes (868/915 MHz)
    participant C  as CC1312R (rfEchoRx.c)
    participant W  as SPI Bus
    participant E  as ESP32-P4 (cc1312_manager.h)

    Note over C: Timer fires — nextNodeListRequestRat elapsed
    C->>C: sendNodeListRequest("retry-empty-list")<br/>clearList(pendingNodes)<br/>writeFrame(UART_MSG_NODE_LIST_REQUEST, ...)
    C->>C: enqueueSpiFrame() — frame on TX queue
    C->>C: serviceSpiTransport()<br/>→ startSpiTxTransfer() → spiTransferMode=2
    C->>C: spiWaitForTxFifoPrimed() → FIFO loaded
    C->>W: DRDY → LOW

    E->>E: update(): DRDY HIGH→LOW detected<br/>_drdyArmed=false
    E->>W: CS → LOW
    E->>W: SPI clock × (2 + LEN + 1)
    W-->>E: [0xAA][LEN][0x05][addr×8][rssi][CRC8]
    E->>W: CS → HIGH

    C->>C: spiTransferCallback() — DRDY HIGH, frame dequeued<br/>armSpiRxTransfer(0)

    E->>E: _parseByte() → _dispatchFrame(CC1312_MSG_LIST_REQUEST)<br/>_pendingListSyncAt = millis() | 1u<br/>Serial.printf("Node list requested — response deferred 80 ms")

    Note over E: 80 ms later — in update()
    E->>E: (millis() - _pendingListSyncAt) >= 80<br/>_pendingListSyncAt = 0<br/>_syncNodeList()

    Note over E: _syncNodeList() builds a single batch buffer
    E->>E: _buildDownlinkFrame(CMD_LIST_ENTRY, node_addr_1) × N<br/>_buildDownlinkFrame(CMD_LIST_END, 0)
    E->>E: _spi.beginTransaction(MODE1)
    E->>W: CS → LOW
    E->>W: SPI clock: all frames in one CS window<br/>[AA LEN 0x10 addr×8 rssi CRC8] × N<br/>[AA LEN 0x11 addr×8 rssi CRC8]
    E->>W: CS → HIGH
    E->>E: _spi.endTransaction()
    E->>E: Serial.printf("[CC1312] Synced N nodes to coordinator")

    Note over C: CS deassert ends DMA RX transfer → spiTransferCallback()
    C->>C: spiTransferCallback() — spiTransferComplete=1 (mode was RX)

    C->>C: serviceSpiTransport()<br/>transferMode==1 (RX), CSN_DEASSERT<br/>loops processUartByte() for each received byte

    C->>C: processUartByte() × bytes<br/>→ processCommandFrame() for CMD_LIST_ENTRY (0x10)<br/>   listAddUnique(pendingNodes, nodeAddr)  [× N entries]

    C->>C: processCommandFrame() for CMD_LIST_END (0x11)<br/>acceptedNodes ← pendingNodes<br/>acceptedNodeCount = N<br/>whitelistReady = 1<br/>nextNodeListRequestRat += 300 s

    Note over C: Coordinator now accepts RF packets from enrolled nodes
    C->>C: startSpiRxTransfer() — armed for next downlink
```

> **Why the 80 ms defer matters:** the CC1312R deasserts DRDY and queues an RX
> DMA re-arm inside `spiTransferCallback()`, but the actual `startSpiRxTransfer()`
> call only happens the next time `serviceSpiTransport()` runs in the main loop — up to
> 50 ms later (the RF RX window). Without the delay the downlink bytes arrive while the
> SSI hardware FIFO has no active DMA consumer and are silently dropped.

---

## 3 — Sensor Node Data Relay

Once `whitelistReady=1`, the CC1312R accepts RF packets from enrolled nodes and
relays them to the ESP32-P4 as SPI uplink frames.

```mermaid
sequenceDiagram
    participant N  as Sensor Node (RF)
    participant C  as CC1312R (rfEchoRx.c)
    participant W  as SPI Bus
    participant E  as ESP32-P4 (cc1312_manager.h)
    participant M  as MQTT Broker

    N->>C: RF packet @ 868/915 MHz<br/>(PIR trigger / temperature / LD2450 / etc.)

    Note over C: echoCallback() fires (RF_EventRxEntryDone)
    C->>C: echoCallback()<br/>rfLinkDecodeFrame(rxFrame)<br/>check whitelistReady && listContains(acceptedNodes, srcAddr)<br/>hasTelemetryFrame = true / hasStatusResponse = true

    Note over C: Back in main loop
    C->>C: writeFrame(UART_MSG_SENSOR_READING, srcAddr, rssi, payload)<br/>→ enqueueSpiFrame()
    C->>C: serviceSpiTransport()<br/>→ startSpiTxTransfer() → spiTransferMode=2
    C->>C: spiWaitForTxFifoPrimed() → FIFO loaded
    C->>W: DRDY → LOW

    E->>E: update(): DRDY HIGH→LOW<br/>_drdyArmed=false
    E->>W: CS → LOW
    E->>W: SPI clock × (2 + LEN + 1)
    W-->>E: [0xAA][LEN][0x02][nodeAddr×8][rssi][sensorData...][CRC8]
    E->>W: CS → HIGH

    C->>C: spiTransferCallback() — DRDY HIGH, frame dequeued

    E->>E: _parseByte() → _dispatchFrame(CC1312_MSG_READING, payload, len)<br/>_upsert(addr, MSG_READING, sensorClass, rssi, data, dataLen)<br/>_nodeLastSeen[i] = millis()

    Note over E: Every 10 s — in update()
    E->>E: _publishPending(now)<br/>→ builds JSON array of latest reading per node
    E->>M: MQTT publish "cc1312/nodes"<br/>[ { "node": "...", "msg": "reading",<br/>    "sensor": "pir", "rssi_dbm": -65, "age_ms": 0, ... } ]
```

---

## 4 — Discovery Mode and Node Enrolment

Discovery mode lets the coordinator surface any RF-visible node. The ESP32-P4 enables
it via a downlink command and receives `NODE_SEEN` frames for each new address spotted.
Enrolment is a separate explicit action.

```mermaid
sequenceDiagram
    participant N  as Unknown RF Node
    participant C  as CC1312R (rfEchoRx.c)
    participant W  as SPI Bus
    participant E  as ESP32-P4 (cc1312_manager.h)
    participant M  as MQTT Broker

    Note over E: handleCommand("discovery_on")
    E->>E: _discoveryMode = true<br/>_discoveryStarted = millis()
    E->>E: _sendDownlink(CMD_DISCOVERY_ON, 0)<br/>_buildDownlinkFrame(0x14, 0, buf)
    E->>W: CS → LOW  [AA LEN 0x14 addr×8 rssi CRC8]
    E->>W: CS → HIGH

    C->>C: serviceSpiTransport() → processUartByte() per byte<br/>→ processCommandFrame(CMD_DISCOVERY_ON)<br/>discoveryMode=1, clearList(seenNodes)

    N->>C: RF packet (any node)
    C->>C: echoCallback() — node not in acceptedNodes but discoveryMode=1<br/>hasSeenNode = true<br/>lastSeenNodeAddr = srcAddr

    C->>C: writeFrame(UART_MSG_NODE_SEEN, srcAddr, rssi, NULL, 0)<br/>→ enqueueSpiFrame() → serviceSpiTransport()<br/>→ startSpiTxTransfer() → spiWaitForTxFifoPrimed()
    C->>W: DRDY → LOW

    E->>E: update(): DRDY LOW detected
    E->>W: CS → LOW / clock / CS → HIGH
    W-->>E: [0xAA][LEN][0x04][addr×8][rssi][CRC8]

    E->>E: _dispatchFrame(CC1312_MSG_NODE_SEEN)<br/>_upsertSeen(addr, rssi)<br/>_publishSeen(addr, rssi)
    E->>M: MQTT publish "cc1312/seen"<br/>{ "node": "0012...", "rssi_dbm": -72 }

    Note over E: Operator approves — handleCommand("accept_node", addr)
    E->>E: _enrollNode(addr) — NVS save<br/>_sendDownlink(CMD_ACCEPT_NODE, addr)
    E->>W: CS LOW → [AA LEN 0x12 addr×8 rssi CRC8] → CS HIGH

    C->>C: processCommandFrame(CMD_ACCEPT_NODE)<br/>listAddUnique(acceptedNodes, addr)<br/>whitelistReady=1

    E->>E: _syncNodeList() — full batch resync
    E->>W: CS LOW → [LIST_ENTRY×N + LIST_END] → CS HIGH

    C->>C: processCommandFrame() × frames<br/>acceptedNodes updated, nextNodeListRequestRat += 300 s

    Note over E: Auto-off after 5 minutes
    E->>E: update(): millis()-_discoveryStarted >= 300000<br/>_discoveryMode=false<br/>_sendDownlink(CMD_DISCOVERY_OFF, 0)
    E->>W: CS LOW → [AA LEN 0x15 ...] → CS HIGH
    C->>C: processCommandFrame(CMD_DISCOVERY_OFF)<br/>discoveryMode=0
```

---

## 5 — Ping / Pong Round-Trip

A simple liveness check. The ESP32-P4 sends `CMD_PING`; the coordinator echoes it as
`MSG_PONG`. The RTT is logged.

```mermaid
sequenceDiagram
    participant E as ESP32-P4 (cc1312_manager.h)
    participant W as SPI Bus
    participant C as CC1312R (rfEchoRx.c)

    Note over E: ping() called (manual or timed)
    E->>E: _lastPingSent = millis()
    E->>E: _sendDownlink(CMD_PING, 0)<br/>_buildDownlinkFrame(0x16, 0, buf)
    E->>W: CS → LOW
    E->>W: SPI clock: [AA LEN 0x16 addr×8 00 CRC8]
    E->>W: CS → HIGH

    C->>C: serviceSpiTransport() → processUartByte() per byte<br/>→ processCommandFrame(CMD_PING)<br/>writeFrame(UART_MSG_PONG, coordinatorAddr, 0, body, bodyLen)
    C->>C: enqueueSpiFrame() → serviceSpiTransport()<br/>→ startSpiTxTransfer() → spiWaitForTxFifoPrimed()
    C->>W: DRDY → LOW

    E->>E: update(): DRDY LOW detected
    E->>W: CS → LOW / clock × (2+LEN+1) / CS → HIGH
    W-->>E: [0xAA][LEN][0x07][addr×8][rssi][CRC8]

    C->>C: spiTransferCallback() — DRDY HIGH, frame dequeued

    E->>E: _dispatchFrame(CC1312_MSG_PONG)<br/>rtt = millis() - _lastPingSent<br/>_lastPingSent = 0<br/>Serial.printf("[CC1312] PONG rtt=%lums")
```

---

## 6 — Periodic Status Poll

The ESP32-P4 broadcasts `CMD_GET_STATUS` (destination `0xFFFF...FF`) every 5 minutes
to refresh firmware version and sensor-type metadata for all enrolled nodes. The
coordinator re-transmits a `GET_STATUS` RF command to each node, which responds with
an RF status payload that the coordinator then relays as a `MSG_STATUS` uplink.

```mermaid
sequenceDiagram
    participant E as ESP32-P4 (cc1312_manager.h)
    participant W as SPI Bus
    participant C as CC1312R (rfEchoRx.c)
    participant N as Sensor Node (RF)

    Note over E: update(): _lastStatusPoll timer elapsed (30 s first, 300 s after)
    E->>E: _sendDownlink(CMD_GET_STATUS, 0xFFFFFFFFFFFFFFFF)
    E->>W: CS LOW → [AA LEN 0x20 FF×8 00 CRC8] → CS HIGH

    C->>C: processCommandFrame(CMD_GET_STATUS, broadcast)<br/>resetDeferredGetStatusForAcceptedNodes()<br/>queueDeferredGetStatus(node) for each accepted node

    Note over C: Main loop — deferred GET_STATUS fires for each node
    C->>C: hasPendingTx = true (scheduleGetStatus(nodeAddr))<br/>pendingTxFrame.msgType = RF_LINK_MSG_GET_STATUS

    C->>N: RF TX: GET_STATUS command → nodeAddr
    N->>C: RF RX: STATUS response (battery, tx_count, fw_ver, sensor_type)

    C->>C: echoCallback() — hasStatusResponse = true<br/>lastStatusSourceAddr = srcAddr<br/>lastStatusPayload = decoded payload

    C->>C: writeFrame(UART_MSG_NODE_STATUS, srcAddr, rssi, payload, len)<br/>→ enqueueSpiFrame() → serviceSpiTransport()<br/>→ startSpiTxTransfer() → spiWaitForTxFifoPrimed()
    C->>W: DRDY → LOW

    E->>E: update(): DRDY LOW detected
    E->>W: CS LOW / clock / CS HIGH
    W-->>E: [0xAA][LEN][0x01][addr×8][rssi][status_payload][CRC8]

    E->>E: _dispatchFrame(CC1312_MSG_STATUS)<br/>_nodeVersion[i] = {major, minor, patch}<br/>_nodeSensorType[i] = sensorClass<br/>_upsert(addr, MSG_STATUS, 0, rssi, data, len)<br/>Serial.printf("[CC1312] status fw=... sensor=...")
```

---

## 7 — SPI State Machine Summary (CC1312R side)

`serviceSpiTransport()` is the core dispatcher called twice per main-loop iteration
(once after `RF_runCmd()`, once after frame writes). Its state transitions are:

```mermaid
stateDiagram-v2
    [*] --> RX_ARMED : startSpiRxTransfer()\nspiTransferMode=1

    RX_ARMED --> RX_COMPLETE : spiTransferCallback()\nCSN_DEASSERT or COMPLETED\nspiTransferComplete=1

    RX_COMPLETE --> PROCESSING : serviceSpiTransport()\nprocessUartByte() per received byte

    PROCESSING --> TX_ARMED : spiFrameQueueCount > 0\nstartSpiTxTransfer()\nspiTransferMode=2\nspiWaitForTxFifoPrimed()\nDRDY → LOW

    PROCESSING --> RX_ARMED : spiFrameQueueCount == 0\nstartSpiRxTransfer()

    TX_ARMED --> TX_COMPLETE : spiTransferCallback()\nDRDY → HIGH\ndequeueSpiFrame()\nif queue empty → armSpiRxTransfer(0)

    TX_COMPLETE --> RX_CANCEL : serviceSpiTransport()\nif RX active and TX queued\nSPI_transferCancel()

    RX_CANCEL --> TX_ARMED : spiTransferCallback()\nstartSpiTxTransfer()

    TX_COMPLETE --> RX_ARMED : serviceSpiTransport()\nno more TX frames\nstartSpiRxTransfer()
```

---

## Key Timing Constants

| Constant | Value | Where set | Meaning |
|---|---|---|---|
| Heartbeat interval | 30 s | `HEARTBEAT_INTERVAL_RAT` | CC1312R periodic uplink |
| Node-list retry (empty) | 10 s | `NODE_LIST_EMPTY_RETRY_RAT` | Retry when `acceptedNodeCount == 0` |
| Node-list keepalive | 300 s | `NODE_LIST_KEEPALIVE_RAT` | Keepalive once list is populated |
| DRDY-to-downlink delay | 80 ms | `_pendingListSyncAt` | ESP32-P4 defers sync after list request |
| RF RX window | ~50 ms | `RF_RX_TIMEOUT` | CC1312R `RF_runCmd()` blocking window |
| DRDY no-data alarm | 35 s | diagnostic block | ESP32-P4 warns if DRDY silent |
| Status poll (first) | 30 s | `CC1312_STATUS_POLL_INITIAL_MS` | ESP32-P4 first GET_STATUS |
| Status poll (repeat) | 300 s | `CC1312_STATUS_POLL_INTERVAL_MS` | ESP32-P4 periodic GET_STATUS |
| Discovery auto-off | 300 s | `handleCommand("discovery_on")` | ESP32-P4 auto-disables discovery |
