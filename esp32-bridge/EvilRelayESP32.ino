// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 7h30th3r0n3. Part of Flipper-EvilRelay: https://github.com/7h30th3r0n3/Flipper-EvilRelay
// EvilRelayESP32: transport bridge for the EvilRelay Flipper FAP.
//
// Role: this ESP32 is cabled to the Flipper's USART (GPIO 13=TX / 14=RX) and
// bridges that UART to the air side, carrying the EvilRelay frame protocol so a
// reader (a phone over Wi-Fi/TCP, or an Evil-Cardputer reader unit over ESP-NOW)
// talks to the Flipper exactly as it would to an Evil emulator node. The Flipper
// does the actual ISO-DEP card emulation; this sketch moves bytes only.
//
//                  Wi-Fi/TCP 5566            UART 115200
//   phone (reader) <------------> ESP32 <----------------> Flipper (ISO-DEP emul) <-> TPE
//   Evil-A (reader) <--ESP-NOW--> ESP32 (same bridge)
//
// Both transports run at once; whichever reader is active feeds the Flipper.
// Outbound REQ prefers a connected TCP client, else broadcasts over ESP-NOW.
//
// Wiring (4 pins, 3.3 V both sides, no level shifter). The board is laid out to
// plug straight onto the Flipper header: UART on GPIO0/GPIO1, 3V3 on the standard
// power pin, and the ground picked up at the GPIO3 position (the Flipper GND lines
// up there), so a short jumper ties the C3 GPIO3 pad to the C3 GND. Leave GPIO3
// unused in code so nothing drives it against ground.
//   Flipper pin 13 (TX)  -> ESP32 GPIO1  (UART_RX_PIN)
//   Flipper pin 14 (RX)  -> ESP32 GPIO0  (UART_TX_PIN)
//   Flipper pin  9 (3V3) -> ESP32 3V3    (standard power pin)
//   Flipper GND          -> ESP32 GND    (via jumper C3 GPIO3 <-> C3 GND; GND sits at the GPIO3 position)
//
// Frame protocol
//   UART / TCP : [0x5A][type][seq][lenHi][lenLo][payload]            (one frame = one message)
//   ESP-NOW    : [0x5A][type][seq][fidx][ftot][plen][payload<=230]   (fragmented, reassembled)
//     type 1 IDENT  reader->Flipper : [uidLen][uid..][atqa0][atqa1][sak][atsLen][ats..]
//     type 2 REQ    Flipper->reader : application APDU from the terminal
//     type 3 RESP   reader->Flipper : card response

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

// ---- config ---------------------------------------------------------------
// Target board: ESP32-C3. Pins below are C3-safe (not strapping/USB/UART0/flash).
// On a classic ESP32 use e.g. 16/17 instead; on an S3 pick any free GPIOs.
#define UART_BAUD    115200          // must match the FAP (RELAY_UART_BAUD)
#define UART_RX_PIN  1               // <- Flipper pin 13 (TX)   [C3 GPIO1]
#define UART_TX_PIN  0               // <- Flipper pin 14 (RX)   [C3 GPIO0]
#define FLIPPER_UART Serial1

static const char* AP_SSID = "EvilRelay";       // same as the Evil firmware
static const char* AP_PASS = "evilrelay1234";
static const int   AP_CHAN = 1;                  // Evil relay uses channel 1
static const uint16_t TCP_PORT = 5566;

#define RLY_MAGIC   0x5A
#define RLY_T_IDENT 1
#define RLY_T_REQ   2
#define RLY_T_RESP  3
#define RLY_CHUNK   230              // ESP-NOW payload per fragment (Evil RLY_CHUNK)

#define PAYLOAD_MAX 512
#define UART_FRAME_MAX (5 + PAYLOAD_MAX)

#define RLY_TRACE 0                  // 1 = print frame flow on USB serial (bring-up)
static const char* typeName(uint8_t t){
  switch(t){ case RLY_T_IDENT:return "IDENT"; case RLY_T_REQ:return "REQ"; case RLY_T_RESP:return "RESP"; default:return "?"; }
}
static void trace(const char* dir, uint8_t type, uint8_t seq, uint16_t len){
#if RLY_TRACE
  Serial.printf("[%s] %s seq=%u len=%u\n", dir, typeName(type), seq, len);
#else
  (void)dir;(void)type;(void)seq;(void)len;
#endif
}

// ---- state ----------------------------------------------------------------
WiFiServer tcpServer(TCP_PORT);
WiFiClient tcpClient;
static uint8_t  bcast[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
static bool     espnowPeerSeen = false;

// ESP-NOW reassembly (one message in flight, half-duplex, like the Evil fw).
static uint8_t  asmBuf[1024];
static uint8_t  asmSeq = 0, asmType = 0, asmTotal = 0, asmHave = 0;
static uint16_t asmLen = 0;
static volatile bool espnowMsgReady = false;
static uint8_t  espnowMsgType = 0, espnowMsgSeq = 0;
static uint16_t espnowMsgLen = 0;
static uint8_t  espnowMsg[1024];

// UART (Serial1) frame reassembly from the Flipper.
static uint8_t  uartBuf[UART_FRAME_MAX];
static size_t   uartHave = 0;
static uint16_t uartNeed = 0;

static uint8_t  outSeq = 0;

// ---- helpers --------------------------------------------------------------
static void sendToFlipper(uint8_t type, uint8_t seq, const uint8_t* p, uint16_t len) {
  if(len > PAYLOAD_MAX) len = PAYLOAD_MAX;
  uint8_t hdr[5] = { RLY_MAGIC, type, seq, (uint8_t)(len >> 8), (uint8_t)(len & 0xFF) };
  FLIPPER_UART.write(hdr, 5);
  if(len) FLIPPER_UART.write(p, len);
  FLIPPER_UART.flush();
}

static void sendToTcp(uint8_t type, uint8_t seq, const uint8_t* p, uint16_t len) {
  if(!tcpClient || !tcpClient.connected()) return;
  uint8_t hdr[5] = { RLY_MAGIC, type, seq, (uint8_t)(len >> 8), (uint8_t)(len & 0xFF) };
  tcpClient.write(hdr, 5);
  if(len) tcpClient.write(p, len);
  tcpClient.flush();
}

static void sendToEspnow(uint8_t type, uint8_t seq, const uint8_t* p, uint16_t len) {
  uint8_t total = (len == 0) ? 1 : (uint8_t)((len + RLY_CHUNK - 1) / RLY_CHUNK);
  for(uint8_t idx = 0; idx < total; idx++) {
    uint16_t off = (uint16_t)idx * RLY_CHUNK;
    uint16_t plen = (len - off > RLY_CHUNK) ? RLY_CHUNK : (len - off);
    uint8_t pkt[6 + RLY_CHUNK];
    pkt[0] = RLY_MAGIC; pkt[1] = type; pkt[2] = seq;
    pkt[3] = idx; pkt[4] = total; pkt[5] = (uint8_t)plen;
    if(plen) memcpy(pkt + 6, p + off, plen);
    esp_now_send(bcast, pkt, 6 + plen);
    delayMicroseconds(300);  // same inter-fragment spacing as the Evil firmware
  }
}

// Route a message coming FROM the Flipper (these are REQ APDUs) to the reader.
// Prefer a connected phone (TCP); otherwise broadcast to an Evil reader unit.
static void routeFromFlipper(uint8_t type, uint8_t seq, const uint8_t* p, uint16_t len) {
  if(tcpClient && tcpClient.connected()) {
    trace("flip->tcp", type, seq, len);
    sendToTcp(type, seq, p, len);
  } else {
    trace("flip->espnow", type, seq, len);
    sendToEspnow(type, seq, p, len);
  }
}

// ---- ESP-NOW receive (reader -> us) ---------------------------------------
// The recv-callback signature changed between arduino-esp32 2.x and 3.x.
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
static void onEspnowRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  (void)info;
#else
static void onEspnowRecv(const uint8_t* mac, const uint8_t* data, int len) {
  (void)mac;
#endif
  if(len < 6 || data[0] != RLY_MAGIC) return;
  uint8_t type = data[1], seq = data[2], fidx = data[3], ftot = data[4], plen = data[5];
  if(6 + plen > len) return;
  espnowPeerSeen = true;

  if(seq != asmSeq || type != asmType) {  // new message
    asmSeq = seq; asmType = type; asmTotal = ftot; asmHave = 0; asmLen = 0;
  }
  uint16_t off = (uint16_t)fidx * RLY_CHUNK;
  if(off + plen <= sizeof(asmBuf)) {
    memcpy(asmBuf + off, data + 6, plen);
    if(off + plen > asmLen) asmLen = off + plen;
    asmHave++;
  }
  if(asmHave >= asmTotal) {              // complete -> hand to main loop
    espnowMsgType = type; espnowMsgSeq = seq; espnowMsgLen = asmLen;
    memcpy(espnowMsg, asmBuf, asmLen);
    espnowMsgReady = true;
    asmSeq = (uint8_t)(seq + 1); asmHave = 0;  // force next to look new
  }
}

// ---- UART (Flipper) byte pump ---------------------------------------------
static void pumpUart() {
  while(FLIPPER_UART.available()) {
    uint8_t b = (uint8_t)FLIPPER_UART.read();
    if(uartHave == 0) {
      if(b != RLY_MAGIC) continue;
      uartBuf[uartHave++] = b;
      continue;
    }
    uartBuf[uartHave++] = b;
    if(uartHave == 5) {
      uartNeed = ((uint16_t)uartBuf[3] << 8) | uartBuf[4];
      if(uartNeed > PAYLOAD_MAX) { uartHave = 0; uartNeed = 0; }
      continue;
    }
    if(uartHave >= 5 && uartHave == (size_t)(5 + uartNeed)) {
      routeFromFlipper(uartBuf[1], uartBuf[2], uartBuf + 5, uartNeed);
      uartHave = 0; uartNeed = 0;
    }
  }
}

// ---- TCP (phone) byte pump ------------------------------------------------
static uint8_t  tcpBuf[UART_FRAME_MAX];
static size_t   tcpHave = 0;
static uint16_t tcpNeed = 0;

static void pumpTcp() {
  if(!tcpClient || !tcpClient.connected()) {
    WiFiClient c = tcpServer.available();
    if(c) {
      if(tcpClient) tcpClient.stop();
      tcpClient = c;
      tcpClient.setNoDelay(true);
      tcpHave = 0; tcpNeed = 0;
      Serial.println("[tcp] phone reader connected");
    }
    return;
  }
  while(tcpClient.available()) {
    uint8_t b = (uint8_t)tcpClient.read();
    if(tcpHave == 0) {
      if(b != RLY_MAGIC) continue;
      tcpBuf[tcpHave++] = b;
      continue;
    }
    tcpBuf[tcpHave++] = b;
    if(tcpHave == 5) {
      tcpNeed = ((uint16_t)tcpBuf[3] << 8) | tcpBuf[4];
      if(tcpNeed > PAYLOAD_MAX) { tcpHave = 0; tcpNeed = 0; }
      continue;
    }
    if(tcpHave >= 5 && tcpHave == (size_t)(5 + tcpNeed)) {
      trace("tcp->flip", tcpBuf[1], tcpBuf[2], tcpNeed);         // IDENT / RESP -> Flipper
      sendToFlipper(tcpBuf[1], tcpBuf[2], tcpBuf + 5, tcpNeed);
      tcpHave = 0; tcpNeed = 0;
    }
  }
}

// ---- setup / loop ---------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(200);
  FLIPPER_UART.begin(UART_BAUD, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID, AP_PASS, AP_CHAN);
  esp_wifi_set_channel(AP_CHAN, WIFI_SECOND_CHAN_NONE);
  tcpServer.begin();
  tcpServer.setNoDelay(true);

  if(esp_now_init() == ESP_OK) {
    esp_now_register_recv_cb(onEspnowRecv);
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, bcast, 6);
    peer.channel = AP_CHAN;
    peer.encrypt = false;
    esp_now_add_peer(&peer);
    Serial.println("[espnow] ready (broadcast ch.1)");
  } else {
    Serial.println("[espnow] init FAILED");
  }

  Serial.print("[ap] SSID "); Serial.print(AP_SSID);
  Serial.print("  IP "); Serial.print(WiFi.softAPIP());
  Serial.print("  TCP "); Serial.println(TCP_PORT);
  Serial.println("[bridge] ready. reader: phone(WiFi) or Evil-A(ESP-NOW) -> Flipper UART");
}

void loop() {
  pumpUart();
  pumpTcp();
  if(espnowMsgReady) {
    espnowMsgReady = false;
    trace("espnow->flip", espnowMsgType, espnowMsgSeq, espnowMsgLen);     // IDENT / RESP -> Flipper
    sendToFlipper(espnowMsgType, espnowMsgSeq, espnowMsg, espnowMsgLen);
  }
}
