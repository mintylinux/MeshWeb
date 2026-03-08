// MeshWeb Companion - ESP32 Xiao with LoRa
// Listens for PAGE_ANNOUNCE broadcasts and requests pages
// Supports both WiFi AP mode and USB-only mode (build with -DUSB_ONLY)

#include <Arduino.h>
#include <SPI.h>
#include <RadioLib.h>
#include <vector>

#ifndef USB_ONLY
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#endif

#ifdef ENABLE_BLE
#include <NimBLEDevice.h>
#endif

// Web protocol
#include "WebProtocol.h"

// Base64 encoding table for getpage command
static const char b64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

String base64_encode(const uint8_t* data, size_t len) {
  String out;
  out.reserve((len + 2) / 3 * 4);
  for (size_t i = 0; i < len; i += 3) {
    uint32_t n = ((uint32_t)data[i]) << 16;
    if (i + 1 < len) n |= ((uint32_t)data[i + 1]) << 8;
    if (i + 2 < len) n |= data[i + 2];
    out += b64_table[(n >> 18) & 0x3F];
    out += b64_table[(n >> 12) & 0x3F];
    out += (i + 1 < len) ? b64_table[(n >> 6) & 0x3F] : '=';
    out += (i + 2 < len) ? b64_table[n & 0x3F] : '=';
  }
  return out;
}

#ifndef USB_ONLY
// Web server (WiFi mode only)
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
#endif

// ============================================================
// BLE UART Transport (Nordic UART Service)
// ============================================================
#ifdef ENABLE_BLE

// Nordic UART Service UUIDs
#define NUS_SERVICE_UUID        "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define NUS_RX_CHARACTERISTIC   "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"  // Client writes here
#define NUS_TX_CHARACTERISTIC   "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"  // Device notifies here

NimBLEServer* pBLEServer = nullptr;
NimBLECharacteristic* pTxCharacteristic = nullptr;
bool bleClientConnected = false;

// Forward declarations
void processCommand(String cmd, Stream* output);

// Note: NimBLE defines macros like '#define BLEServerCallbacks NimBLEServerCallbacks'
// so we use MeshWeb-prefixed names to avoid redefinition collisions.
class MeshWebServerCB : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* pServer) override {
    bleClientConnected = true;
    Serial.println("BLE client connected");
    NimBLEDevice::startAdvertising();
  }
  void onDisconnect(NimBLEServer* pServer) override {
    bleClientConnected = false;
    Serial.println("BLE client disconnected");
    NimBLEDevice::startAdvertising();
  }
};

// Stream wrapper that sends processCommand output back over BLE TX
class BLEStream : public Stream {
  String _lineBuf;
public:
  // Print interface - this is what processCommand uses
  size_t write(uint8_t c) override {
    _lineBuf += (char)c;
    if (c == '\n') {
      _flush();
    }
    return 1;
  }
  size_t write(const uint8_t* buf, size_t size) override {
    for (size_t i = 0; i < size; i++) {
      _lineBuf += (char)buf[i];
      if (buf[i] == '\n') {
        _flush();
      }
    }
    return size;
  }
  void _flush() {
    if (_lineBuf.length() == 0 || !bleClientConnected || !pTxCharacteristic) {
      _lineBuf = "";
      return;
    }
    const uint8_t* data = (const uint8_t*)_lineBuf.c_str();
    size_t len = _lineBuf.length();
    size_t mtu = NimBLEDevice::getMTU() - 3;
    if (mtu < 20) mtu = 20;
    for (size_t offset = 0; offset < len; offset += mtu) {
      size_t chunkLen = len - offset;
      if (chunkLen > mtu) chunkLen = mtu;
      pTxCharacteristic->setValue(data + offset, chunkLen);
      pTxCharacteristic->notify();
      if (offset + chunkLen < len) delay(5);
    }
    // Delay after each line to prevent notification queue overflow
    delay(15);
    // Also echo to Serial for debugging
    Serial.print("[BLE>] ");
    Serial.print(_lineBuf);
    _lineBuf = "";
  }
  // Stream interface (unused - we only use Print side)
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
};

BLEStream bleOutputStream;

// BLE command queue - process in loop() instead of blocking NimBLE task
String pendingBLECommand = "";
// Auto-send page data over BLE after page_complete
bool bleAutoSendPage = false;

class MeshWebRxCB : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* pCharacteristic) override {
    std::string value = pCharacteristic->getValue();
    if (value.length() > 0) {
      String cmd = String(value.c_str());
      cmd.trim();
      Serial.printf("[BLE] Command queued: %s\n", cmd.c_str());
      pendingBLECommand = cmd;  // Queue for processing in loop()
    }
  }
};

// Forward declare - companionName is defined later with other globals
extern char companionName[32];

void setupBLE() {
  String bleName = "MeshWeb-" + String(companionName);
  Serial.printf("[BLE] Initializing as '%s'...\n", bleName.c_str());
  Serial.printf("[BLE] Free heap before init: %d\n", ESP.getFreeHeap());
  
  NimBLEDevice::init(bleName.c_str());
  
  if (!NimBLEDevice::getInitialized()) {
    Serial.println("[BLE] ERROR: NimBLE init FAILED!");
    return;
  }
  Serial.println("[BLE] NimBLE initialized OK");
  
  NimBLEDevice::setMTU(512);
  
  pBLEServer = NimBLEDevice::createServer();
  if (!pBLEServer) {
    Serial.println("[BLE] ERROR: createServer() returned NULL!");
    return;
  }
  pBLEServer->setCallbacks(new MeshWebServerCB());
  Serial.println("[BLE] Server created");
  
  NimBLEService* pService = pBLEServer->createService(NUS_SERVICE_UUID);
  if (!pService) {
    Serial.println("[BLE] ERROR: createService() returned NULL!");
    return;
  }
  
  pTxCharacteristic = pService->createCharacteristic(
    NUS_TX_CHARACTERISTIC,
    NIMBLE_PROPERTY::NOTIFY
  );
  
  NimBLECharacteristic* pRxCharacteristic = pService->createCharacteristic(
    NUS_RX_CHARACTERISTIC,
    NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
  );
  pRxCharacteristic->setCallbacks(new MeshWebRxCB());
  
  pService->start();
  Serial.println("[BLE] NUS service started");
  
  NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(NUS_SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->start();
  
  Serial.printf("[BLE] Free heap after init: %d\n", ESP.getFreeHeap());
  Serial.printf("[BLE] Advertising started: %s\n", bleName.c_str());
  Serial.println("BLE UART started: " + bleName);
}

// Send JSON event over BLE (chunked to fit MTU)
void bleSendEvent(const String& json) {
  if (!bleClientConnected || !pTxCharacteristic) return;
  
  // Add EVT: prefix for consistency with serial protocol
  String msg = "EVT:" + json + "\n";
  const uint8_t* data = (const uint8_t*)msg.c_str();
  size_t len = msg.length();
  
  // Get negotiated MTU minus 3 bytes ATT overhead
  size_t mtu = NimBLEDevice::getMTU() - 3;
  if (mtu < 20) mtu = 20;
  
  for (size_t offset = 0; offset < len; offset += mtu) {
    size_t chunkLen = len - offset;
    if (chunkLen > mtu) chunkLen = mtu;
    pTxCharacteristic->setValue(data + offset, chunkLen);
    pTxCharacteristic->notify();
    if (offset + chunkLen < len) delay(5);  // Small delay between chunks
  }
}

#endif  // ENABLE_BLE

// Send a JSON event to all connected clients (WebSocket + BLE + Serial)
void sendEvent(const String& json) {
#ifndef USB_ONLY
  ws.textAll(json);
#endif
#ifdef ENABLE_BLE
  bleSendEvent(json);
#endif
  Serial.println("EVT:" + json);
}

// LoRa radio pins (from platformio.ini build flags)
// These are automatically set for XIAO ESP32-S3 + Wio-SX1262 Kit

// Create radio object (adjust for your LoRa module type)
// SX1262 for newer modules, SX1278 for older RFM95/96
SX1262 radio = new Module(LORA_CS, LORA_DIO1, LORA_RST, LORA_BUSY);
// If you have SX1278/RFM95, uncomment this instead:
// SX1278 radio = new Module(LORA_CS, LORA_DIO1, LORA_RST);

// Node identity
uint8_t node_id[4];

// Radio state
bool loraReady = false;
volatile bool receivedFlag = false;
bool inFSKMode = false;  // Track current mode

// Discovered web nodes
struct WebNode {
  uint8_t node_id[4];
  char name[32];  // Friendly name
  unsigned long last_seen;
  char pages[WEB_MAX_PAGES_ANNOUNCE][WEB_MAX_PATH_LEN];
  int page_count;
};
std::vector<WebNode> discoveredNodes;

// Discovered companions
struct Companion {
  uint8_t node_id[4];
  char name[32];
  uint8_t status;  // 0=available, 1=busy
  unsigned long last_seen;
};
std::vector<Companion> discoveredCompanions;

// Companion messaging
struct ChatMessage {
  uint8_t from_id[4];
  char from_name[32];
  char message[WEB_MAX_MESSAGE_LEN];
  unsigned long timestamp;
  bool is_broadcast;
};
std::vector<ChatMessage> chatHistory;
const int MAX_CHAT_HISTORY = 20;

// Companion announce timer
unsigned long lastCompanionAnnounce = 0;
const unsigned long COMPANION_ANNOUNCE_INTERVAL = 30000; // 30 seconds
char companionName[32];  // Our friendly name
uint8_t nextMsgId = 0;

// Generate a friendly name from node ID
String generateNodeName(uint8_t* id) {
  const char* adjectives[] = {"Red", "Blue", "Green", "Swift", "Bright", "Dark", "Wild", "Calm"};
  const char* nouns[] = {"Fox", "Wolf", "Hawk", "Bear", "Eagle", "Tiger", "Lion", "Raven"};
  
  // Use node ID bytes to pick words
  int adjIdx = id[0] % 8;
  int nounIdx = id[1] % 8;
  
  return String(adjectives[adjIdx]) + " " + String(nouns[nounIdx]);
}

// Page request state
bool requestPending = false;
unsigned long requestTime = 0;
const unsigned long REQUEST_TIMEOUT = 30000; // 30 seconds
uint8_t currentRequestId = 0;  // Track our request ID to filter responses

// Page reassembly
String currentPage = "";
int receivedChunks = 0;
int totalChunksExpected = 0;
bool receivingPage = false;
unsigned long lastChunkTime = 0;  // For stall detection

// Forward declarations (also declared above BLE callbacks when BLE enabled)
#ifndef ENABLE_BLE
void processCommand(String cmd, Stream* output);
#endif

// Interrupt handler for LoRa receive
void IRAM_ATTR setFlag(void) {
  receivedFlag = true;
}

// Switch to fast LoRa mode for high-speed data transfer
void switchToFSK() {
  if (inFSKMode) return;  // Already in fast mode
  
  Serial.println("⚡ Switching to fast LoRa mode (SF7)");
  
  // Configure fast LoRa: SF7 + 500kHz BW = ~21.9 kbps (12x faster than SF11)
  int state = radio.begin(
    915.0,      // Frequency: 915.0 MHz
    500.0,      // Bandwidth: 500 kHz (maximum)
    7,          // Spreading Factor: 7 (minimum for max speed)
    5,          // Coding Rate: 4/5
    0x12,       // Sync Word
    22,         // Output power: 22 dBm
    8,          // Preamble length
    1.8         // TCXO voltage
  );
  
  if (state == RADIOLIB_ERR_NONE) {
    Serial.println("✓ Fast LoRa mode active (SF7)");
    inFSKMode = true;  // Using this flag to track "fast mode"
  } else {
    Serial.printf("✗ Fast mode switch failed: %d\n", state);
  }
}

// Switch to LoRa mode for long-range broadcasts
void switchToLoRa() {
  if (!inFSKMode) return;  // Already in LoRa
  
  Serial.println("📡 Switching to LoRa mode (long-range)");
  
  int state = radio.begin(
    915.0,      // Frequency: 915.0 MHz
    250.0,      // Bandwidth: 250 kHz
    9,          // Spreading Factor: 9 (balance of speed and reliability)
    5,          // Coding Rate: 4/5
    0x12,       // Sync Word
    22,         // Output power: 22 dBm
    8,          // Preamble length
    1.8         // TCXO voltage
  );
  
  if (state == RADIOLIB_ERR_NONE) {
    Serial.println("✓ LoRa mode active");
    inFSKMode = false;
  } else {
    Serial.printf("✗ LoRa switch failed: %d\n", state);
  }
}

void setupLoRa() {
  Serial.println("\nInitializing LoRa...");
  Serial.printf("Pin config: SCK=%d MISO=%d MOSI=%d CS=%d RST=%d DIO1=%d BUSY=%d\n",
                LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS, LORA_RST, LORA_DIO1, LORA_BUSY);
  
  // Hardware reset for SX1262
  pinMode(LORA_RST, OUTPUT);
  digitalWrite(LORA_RST, LOW);
  delay(100);
  digitalWrite(LORA_RST, HIGH);
  delay(100);
  Serial.println("Reset pulse sent");
  
  // Initialize SPI with explicit settings
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
  SPI.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));
  SPI.endTransaction();
  Serial.println("SPI initialized");
  
  // Set antenna switch control (DIO2 as RF switch)
  radio.setDio2AsRfSwitch(true);
  
  // Initialize radio (same config as web node)
  int state = radio.begin(915.0,   // Frequency: 915.0 MHz (US ISM band)
                         250.0,     // Bandwidth: 250 kHz
                         9,         // Spreading Factor: 9 (balance of speed and reliability)
                         5,         // Coding Rate: 4/5
                         0x12,      // Sync Word (same as web node!)
                         22,        // Output power: 22 dBm
                         8,         // Preamble length
                         1.8);      // TCXO voltage
  
  if (state == RADIOLIB_ERR_NONE) {
    Serial.println("✓ LoRa init success!");
    Serial.println("✓ Frequency: 915.0 MHz (US ISM Band)");
    Serial.println("✓ Listening for PAGE_ANNOUNCE broadcasts...");
    loraReady = true;
    
    // Set interrupt for receive
    radio.setDio1Action(setFlag);
    
    // Start listening
    radio.startReceive();
    
  } else {
    Serial.print("✗ LoRa init failed, code: ");
    Serial.println(state);
    Serial.println("\nTroubleshooting:");
    Serial.println("- Check wiring (SCK, MISO, MOSI, CS, RST, DIO1, BUSY)");
    Serial.println("- Verify pin definitions match your hardware");
    Serial.println("- If using SX1278/RFM95, change radio type in code");
    Serial.println("- If using SX1262, ensure TCXO voltage is correct");
    loraReady = false;
  }
}

// Find or add node to discovered list
WebNode* findOrAddNode(uint8_t* id) {
  // Search for existing
  for (auto &node : discoveredNodes) {
    if (memcmp(node.node_id, id, 4) == 0) {
      return &node;
    }
  }
  
  // Add new
  WebNode newNode;
  memcpy(newNode.node_id, id, 4);
  String friendlyName = generateNodeName(id);
  strncpy(newNode.name, friendlyName.c_str(), 31);
  newNode.name[31] = '\0';
  newNode.last_seen = millis();
  newNode.page_count = 0;
  discoveredNodes.push_back(newNode);
  return &discoveredNodes.back();
}

// Display discovered nodes
void printDiscoveredNodes() {
  Serial.println("\n╔════════════════════════════════════════════╗");
  Serial.println("║      Discovered Web Nodes                  ║");
  Serial.println("╠════════════════════════════════════════════╣");
  
  if (discoveredNodes.empty()) {
    Serial.println("║  (none)                                    ║");
  } else {
    for (size_t i = 0; i < discoveredNodes.size(); i++) {
      auto &node = discoveredNodes[i];
      Serial.printf("║ [%d] %s                              ║\n", i, node.name);
      Serial.printf("║     ID: %02X%02X%02X%02X                         ║\n", 
                    node.node_id[0], node.node_id[1], 
                    node.node_id[2], node.node_id[3]);
      Serial.printf("║     Pages: %d                              ║\n", node.page_count);
      for (int p = 0; p < node.page_count; p++) {
        Serial.printf("║       - %-30s ║\n", node.pages[p]);
      }
      
      unsigned long ago = (millis() - node.last_seen) / 1000;
      Serial.printf("║     Last seen: %ld seconds ago            ║\n", ago);
      Serial.println("╠════════════════════════════════════════════╣");
    }
  }
  
  Serial.println("╚════════════════════════════════════════════╝\n");
}

// Find or add companion to discovered list
Companion* findOrAddCompanion(uint8_t* id) {
  // Don't add ourselves
  if (memcmp(id, node_id, 4) == 0) return nullptr;
  
  // Search for existing
  for (auto &comp : discoveredCompanions) {
    if (memcmp(comp.node_id, id, 4) == 0) {
      return &comp;
    }
  }
  
  // Add new
  Companion newComp;
  memcpy(newComp.node_id, id, 4);
  String friendlyName = generateNodeName(id);
  strncpy(newComp.name, friendlyName.c_str(), 31);
  newComp.name[31] = '\0';
  newComp.status = 0;
  newComp.last_seen = millis();
  discoveredCompanions.push_back(newComp);
  return &discoveredCompanions.back();
}

// Send companion announce broadcast
void sendCompanionAnnounce() {
  if (!loraReady) return;
  
  WebCompanionAnnounce announce;
  announce.msg_type = WEB_MSG_COMPANION_ANNOUNCE;
  memcpy(announce.node_id, node_id, 4);
  strncpy(announce.name, companionName, 31);
  announce.name[31] = '\0';
  announce.status = 0;  // Available
  announce.timestamp = millis();
  
  uint8_t buf[256];
  int len = announce.writeTo(buf);
  
  int state = radio.transmit(buf, len);
  if (state == RADIOLIB_ERR_NONE) {
    Serial.println("📢 Companion announce sent");
  }
  
  radio.startReceive();
}

// Send a message to a companion (or broadcast)
void sendCompanionMessage(uint8_t* to_id, const char* message) {
  if (!loraReady) return;
  
  WebCompanionMessage msg;
  msg.msg_type = WEB_MSG_COMPANION_MESSAGE;
  memcpy(msg.from_id, node_id, 4);
  if (to_id) {
    memcpy(msg.to_id, to_id, 4);
  } else {
    // Broadcast
    memset(msg.to_id, 0xFF, 4);
  }
  msg.msg_id = nextMsgId++;
  strncpy(msg.message, message, WEB_MAX_MESSAGE_LEN - 1);
  msg.message[WEB_MAX_MESSAGE_LEN - 1] = '\0';
  
  uint8_t buf[256];
  int len = msg.writeTo(buf);
  
  int state = radio.transmit(buf, len);
  if (state == RADIOLIB_ERR_NONE) {
    Serial.printf("💬 Message sent over LoRa: %s\n", message);
  } else {
    Serial.printf("⚠ LoRa send failed (code %d), message shown locally only: %s\n", state, message);
  }
  
  radio.startReceive();
  
  // Always add to chat history and notify browser (even if LoRa failed)
  // so the user sees their own message in the UI
  ChatMessage chatMsg;
  memcpy(chatMsg.from_id, node_id, 4);
  strncpy(chatMsg.from_name, companionName, 31);
  chatMsg.from_name[31] = '\0';
  strncpy(chatMsg.message, message, WEB_MAX_MESSAGE_LEN - 1);
  chatMsg.message[WEB_MAX_MESSAGE_LEN - 1] = '\0';
  chatMsg.timestamp = millis();
  chatMsg.is_broadcast = (to_id == nullptr);
  
  chatHistory.push_back(chatMsg);
  if (chatHistory.size() > MAX_CHAT_HISTORY) {
    chatHistory.erase(chatHistory.begin());
  }
  
  char fromIdStr[16];
  sprintf(fromIdStr, "%02x%02x%02x%02x", node_id[0], node_id[1], node_id[2], node_id[3]);
  String json = "{\"type\":\"chat\",\"from\":\"" + String(companionName) + "\",\"from_id\":\"" + String(fromIdStr) + "\",\"msg\":\"" + String(message) + "\",\"self\":true,\"broadcast\":" + String(to_id == nullptr ? "true" : "false") + "}";
  if (to_id) {
    char toIdStr[16];
    sprintf(toIdStr, "%02x%02x%02x%02x", to_id[0], to_id[1], to_id[2], to_id[3]);
    json = json.substring(0, json.length() - 1) + ",\"to_id\":\"" + String(toIdStr) + "\"}";
  }
  sendEvent(json);
}

// Request a page from a node
void requestPage(uint8_t* target_node, const char* page_path) {
  if (!loraReady) return;
  
  // Clear old page data to free memory
  currentPage = "";
  receivedChunks = 0;
  totalChunksExpected = 0;
  receivingPage = false;
  requestPending = false;
  
  Serial.printf("\n→ Requesting page: %s\n", page_path);
  Serial.printf("  From node: %02X%02X%02X%02X\n",
                target_node[0], target_node[1], 
                target_node[2], target_node[3]);
  Serial.printf("  Free heap: %d bytes\n", ESP.getFreeHeap());
  
  WebPageRequest req;
  req.msg_type = WEB_MSG_PAGE_REQUEST;
  currentRequestId = random(1, 256);  // Random request ID (1-255, 0 = no request)
  req.request_id = currentRequestId;
  memcpy(req.target_node, target_node, 4);
  req.chunk_index = 0;  // Start from first chunk
  strncpy(req.page_path, page_path, WEB_MAX_PATH_LEN);
  
  uint8_t buf[256];
  int len = req.writeTo(buf);
  
  Serial.printf("  Request ID: %d\n", req.request_id);
  
  // Send request in slow LoRa mode (so host can receive it)
  // Host is always listening in SF11 mode
  int state = radio.transmit(buf, len);
  
  if (state == RADIOLIB_ERR_NONE) {
    Serial.println("✓ Request sent (SF11)!");
    requestPending = true;
    requestTime = millis();
  } else {
    Serial.printf("✗ Request failed, code: %d\n", state);
  }
  
  // Stay in SF9 mode - no mode switching
  // Return to receive mode
  radio.startReceive();
  Serial.println("⏳ Waiting for PAGE_DATA in SF9 mode...");
}

// Handle received LoRa packet
void handleLoRaPacket() {
  if (!loraReady) return;
  
  uint8_t buf[256];
  int state = radio.readData(buf, 256);
  
  // Re-arm receiver IMMEDIATELY so we don't miss the next packet
  // (data is already copied into buf, safe to start listening again)
  radio.startReceive();
  
  if (state == RADIOLIB_ERR_NONE) {
    int len = radio.getPacketLength();
    float rssi = radio.getRSSI();
    float snr = radio.getSNR();
    
    uint8_t msg_type = WebProtocol::getMessageType(buf);
    
    // During active page transfer, skip non-essential messages to avoid delays
    // But always allow companion messages through
    if (receivingPage && msg_type != WEB_MSG_PAGE_DATA && msg_type != WEB_MSG_COMPANION_MESSAGE) {
      Serial.printf("⏭ Skipping msg 0x%02X during page transfer (%d/%d)\n",
                    msg_type, receivedChunks, totalChunksExpected);
      return;
    }
    
    switch (msg_type) {
      case WEB_MSG_PAGE_ANNOUNCE: {
        // Already in SF11 - no mode switching needed
        
        WebPageAnnounce announce;
        if (announce.readFrom(buf, len)) {
          Serial.println("\n╔════════════════════════════════════════════╗");
          Serial.println("║  📡 PAGE_ANNOUNCE Received                 ║");
          Serial.println("╠════════════════════════════════════════════╣");
          Serial.printf("║  Node: %-33s ║\n", announce.node_name);
          Serial.printf("║  ID: %02X%02X%02X%02X                            ║\n",
                       announce.node_id[0], announce.node_id[1],
                       announce.node_id[2], announce.node_id[3]);
          Serial.printf("║  RSSI: %.1f dBm  SNR: %.1f dB            ║\n", rssi, snr);
          Serial.printf("║  Pages: %d                                 ║\n", announce.page_count);
          
          // Update discovered nodes
          WebNode* node = findOrAddNode(announce.node_id);
          // Use broadcast name if available, otherwise generate one
          if (strlen(announce.node_name) > 0) {
            strncpy(node->name, announce.node_name, 31);
            node->name[31] = '\0';
          }
          node->last_seen = millis();
          node->page_count = announce.page_count;
          for (int i = 0; i < announce.page_count; i++) {
            strncpy(node->pages[i], announce.pages[i], WEB_MAX_PATH_LEN);
            Serial.printf("║    %d. %-34s ║\n", i+1, announce.pages[i]);
          }
          
          Serial.println("╚════════════════════════════════════════════╝");
          
          // Send updated node list to all clients
          {
            String nodesJson = "{\"type\":\"nodes\",\"nodes\":[";
            for (size_t i = 0; i < discoveredNodes.size(); i++) {
              auto &n = discoveredNodes[i];
              if (i > 0) nodesJson += ",";
              nodesJson += "{\"index\":" + String(i) + ",";
              nodesJson += "\"name\":\"" + String(n.name) + "\",";
              nodesJson += "\"id\":\"" + String(n.node_id[0], HEX) + String(n.node_id[1], HEX) + String(n.node_id[2], HEX) + String(n.node_id[3], HEX) + "\",";
              nodesJson += "\"pages\":[";
              for (int p = 0; p < n.page_count; p++) {
                if (p > 0) nodesJson += ",";
                nodesJson += "\"" + String(n.pages[p]) + "\"";
              }
              nodesJson += "]}";
            }
            nodesJson += "]}";
            sendEvent(nodesJson);
            Serial.println("  → Node list sent to clients");
          }
        }
        break;
      }
      
      case WEB_MSG_PAGE_REQUEST: {
        // Ignore - this is likely our own transmission being echoed
        Serial.println("⚠ Ignoring PAGE_REQUEST (our own transmission)");
        return;
      }
      
      case WEB_MSG_PAGE_DATA: {
        WebPageData data;
        if (data.readFrom(buf, len)) {
          // Only process if this is a response to OUR request
          if (currentRequestId == 0 || data.request_id != currentRequestId) {
            Serial.printf("⚠ Ignoring PAGE_DATA (req_id %d, ours %d)\n", data.request_id, currentRequestId);
            break;
          }
          
          Serial.printf("📄 Chunk %d/%d (%d bytes) RSSI: %.1f dBm\n", 
                       data.chunk_index + 1, data.total_chunks, data.data_len, rssi);
          lastChunkTime = millis();
          
          // Start new page reassembly
          if (data.chunk_index == 0) {
            currentPage = "";
            // Pre-allocate to avoid repeated reallocation
            currentPage.reserve(data.total_chunks * WEB_MAX_CHUNK_SIZE);
            receivedChunks = 0;
            totalChunksExpected = data.total_chunks;
            receivingPage = true;
            
            // Send start marker to all clients
            Serial.println("  → Sending page_start");
            sendEvent("{\"type\":\"page_start\"}");
          }
          
          // Append chunk data (bulk concat - avoid char-by-char reallocation)
          if (receivingPage) {
            char tmpBuf[WEB_MAX_CHUNK_SIZE + 1];
            memcpy(tmpBuf, data.data, data.data_len);
            tmpBuf[data.data_len] = '\0';
            currentPage += tmpBuf;
            receivedChunks++;
            
            // Send progress to all clients
            String progressMsg = "{\"type\":\"progress\",\"received\":" + String(receivedChunks) + ",\"total\":" + String(totalChunksExpected) + "}";
            Serial.printf("  → Progress: %d/%d\n", receivedChunks, totalChunksExpected);
            sendEvent(progressMsg);
            
            // Check if complete
            if (receivedChunks >= totalChunksExpected) {
              Serial.println("✓ Page complete!");
              Serial.printf("   HTML size: %d bytes\n", currentPage.length());
              Serial.printf("   Free heap: %d bytes\n", ESP.getFreeHeap());
              
              // Just signal completion - let the app display the stored HTML
              sendEvent("{\"type\":\"page_complete\",\"size\":" + String(currentPage.length()) + "}");
              
#ifdef ENABLE_BLE
              bleAutoSendPage = true;  // Auto-send page data to BLE client
#endif
              Serial.println("   Page ready for display");
              receivingPage = false;
              requestPending = false;
              currentRequestId = 0;  // Clear so we don't accept stale data
            }
          }
        }
        break;
      }
      
      case WEB_MSG_COMPANION_ANNOUNCE: {
        WebCompanionAnnounce announce;
        if (announce.readFrom(buf, len)) {
          // Don't process our own announces
          if (memcmp(announce.node_id, node_id, 4) == 0) break;
          
          Serial.printf("\n👤 Companion discovered: %s (RSSI: %.1f)\n", announce.name, rssi);
          
          Companion* comp = findOrAddCompanion(announce.node_id);
          if (comp) {
            strncpy(comp->name, announce.name, 31);
            comp->name[31] = '\0';
            comp->status = announce.status;
            comp->last_seen = millis();
            
            // Notify all clients
            String json = "{\"type\":\"companions\",\"companions\":[";
            for (size_t i = 0; i < discoveredCompanions.size(); i++) {
              auto &c = discoveredCompanions[i];
              if (i > 0) json += ",";
              char idStr[16];
              sprintf(idStr, "%02x%02x%02x%02x", c.node_id[0], c.node_id[1], c.node_id[2], c.node_id[3]);
              json += "{\"index\":" + String(i) + ",\"name\":\"" + String(c.name) + "\",\"id\":\"" + String(idStr) + "\",\"status\":" + String(c.status) + "}";
            }
            json += "]}";
            sendEvent(json);
          }
        }
        break;
      }
      
      case WEB_MSG_COMPANION_MESSAGE: {
        WebCompanionMessage msg;
        if (msg.readFrom(buf, len)) {
          // Don't process our own messages
          if (memcmp(msg.from_id, node_id, 4) == 0) break;
          
          // Check if message is for us or broadcast
          bool forUs = msg.isBroadcast() || memcmp(msg.to_id, node_id, 4) == 0;
          if (!forUs) break;
          
          // Find sender name
          String senderName = "Unknown";
          for (auto &c : discoveredCompanions) {
            if (memcmp(c.node_id, msg.from_id, 4) == 0) {
              senderName = String(c.name);
              break;
            }
          }
          
          Serial.printf("\n💬 Message from %s: %s\n", senderName.c_str(), msg.message);
          
          // Add to chat history
          ChatMessage chatMsg;
          memcpy(chatMsg.from_id, msg.from_id, 4);
          strncpy(chatMsg.from_name, senderName.c_str(), 31);
          chatMsg.from_name[31] = '\0';
          strncpy(chatMsg.message, msg.message, WEB_MAX_MESSAGE_LEN - 1);
          chatMsg.message[WEB_MAX_MESSAGE_LEN - 1] = '\0';
          chatMsg.timestamp = millis();
          chatMsg.is_broadcast = msg.isBroadcast();
          
          chatHistory.push_back(chatMsg);
          if (chatHistory.size() > MAX_CHAT_HISTORY) {
            chatHistory.erase(chatHistory.begin());
          }
          
          // Notify all clients
          char senderIdStr[16];
          sprintf(senderIdStr, "%02x%02x%02x%02x", msg.from_id[0], msg.from_id[1], msg.from_id[2], msg.from_id[3]);
          String json = "{\"type\":\"chat\",\"from\":\"" + senderName + "\",\"from_id\":\"" + String(senderIdStr) + "\",\"msg\":\"" + String(msg.message) + "\",\"self\":false,\"broadcast\":" + String(msg.isBroadcast() ? "true" : "false") + "}";
          if (!msg.isBroadcast()) {
            char toIdStr[16];
            sprintf(toIdStr, "%02x%02x%02x%02x", msg.to_id[0], msg.to_id[1], msg.to_id[2], msg.to_id[3]);
            json = json.substring(0, json.length() - 1) + ",\"to_id\":\"" + String(toIdStr) + "\"}";
          }
          sendEvent(json);
        }
        break;
      }
      
      default:
        Serial.printf("⚠ Unknown message type: 0x%02X\n", msg_type);
    }
    
  } else if (state == RADIOLIB_ERR_CRC_MISMATCH) {
    Serial.println("⚠ CRC error - corrupted packet");
  }
}

#ifndef USB_ONLY
// WebSocket event handler
void onWebSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, 
                      AwsEventType type, void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    Serial.printf("WebSocket client #%u connected\n", client->id());
  } else if (type == WS_EVT_DISCONNECT) {
    Serial.printf("WebSocket client #%u disconnected\n", client->id());
  } else if (type == WS_EVT_DATA) {
    AwsFrameInfo *info = (AwsFrameInfo*)arg;
    if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
      data[len] = 0;
      String cmd = String((char*)data);
      processCommand(cmd, &Serial); // Process commands from WebSocket
    }
  }
}
#endif

#ifndef USB_ONLY
// Setup web server (WiFi mode only)
void setupWebServer() {
  // Serve HTML with tabs for Nodes, Companions, and Chat
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html", 
      "<html><head><style>"
      "*{margin:0;padding:0;box-sizing:border-box}body{font-family:Arial;background:#111;color:#fff;padding:10px}"
      "h2{margin:10px 0}.tabs{display:flex;gap:5px;margin:10px 0}.tab{background:#333;border:none;padding:10px 20px;color:#fff;cursor:pointer;border-radius:5px 5px 0 0}"
      ".tab.active{background:#07f}.panel{display:none;background:#222;padding:15px;border-radius:0 5px 5px 5px;min-height:200px}.panel.active{display:block}"
      "button{background:#07f;border:none;padding:8px 12px;color:#fff;cursor:pointer;border-radius:4px;margin:2px}button:hover{background:#05d}"
      ".node,.comp{margin:10px 0;padding:10px;background:#333;border-radius:5px}.nid{color:#888;font-size:11px;font-family:monospace}"
      "#chat{height:200px;overflow-y:auto;background:#1a1a1a;padding:10px;border-radius:5px;margin-bottom:10px}"
      ".msg{margin:5px 0;padding:5px;border-radius:3px}.msg.self{background:#07f;text-align:right}.msg.other{background:#333}"
      ".msg-from{font-size:11px;color:#888}#msgInput{width:70%;padding:8px;border:none;border-radius:4px;background:#333;color:#fff}"
      "#status{background:#1a1a1a;padding:10px;border-radius:5px;margin:10px 0}"
      "</style></head><body>"
      "<h2>📡 MeshWeb Companion</h2>"
      "<div class=tabs><button class='tab active' onclick='showTab(0)'>Nodes</button><button class=tab onclick='showTab(1)'>Companions</button><button class=tab onclick='showTab(2)'>Chat</button></div>"
      "<div id=p0 class='panel active'><div id=nodes>Loading...</div></div>"
      "<div id=p1 class=panel><div id=comps>No companions discovered yet</div></div>"
      "<div id=p2 class=panel><div id=chat></div><input id=msgInput placeholder='Type message...'><button onclick='sendMsg()'>Send</button></div>"
      "<div id=status></div>"
      "<script>"
      "var ws=new WebSocket('ws://'+location.host+'/ws');"
      "function showTab(n){document.querySelectorAll('.tab').forEach((t,i)=>{t.className=i==n?'tab active':'tab'});document.querySelectorAll('.panel').forEach((p,i)=>{p.className=i==n?'panel active':'panel'})}"
      "function sendMsg(){var m=document.getElementById('msgInput').value;if(m){ws.send('msg '+m);document.getElementById('msgInput').value=''}}"
      "document.getElementById('msgInput').onkeypress=function(e){if(e.key=='Enter')sendMsg()};"
      "ws.onmessage=function(e){try{var d=JSON.parse(e.data);"
      "if(d.type=='nodes'){var h='';for(var i=0;i<d.nodes.length;i++){var n=d.nodes[i];h+='<div class=node><b>'+n.name+'</b> <span class=nid>ID: '+n.id+'</span><br>';for(var j=0;j<n.pages.length;j++){h+='<button onclick=\"ws.send(\\'req '+n.index+' '+n.pages[j]+'\\')\">'+n.pages[j]+'</button>'}h+='</div>'}document.getElementById('nodes').innerHTML=h||'No nodes discovered'}"
      "else if(d.type=='companions'){var h='';for(var i=0;i<d.companions.length;i++){var c=d.companions[i];h+='<div class=comp><b>'+c.name+'</b> <span class=nid>ID: '+c.id+'</span></div>'}document.getElementById('comps').innerHTML=h||'No companions'}"
      "else if(d.type=='chat'){var c=document.getElementById('chat');c.innerHTML+='<div class=\"msg '+(d.self?'self':'other')+'\"><span class=msg-from>'+d.from+'</span><br>'+d.msg+'</div>';c.scrollTop=c.scrollHeight}"
      "else if(d.type=='progress'){document.getElementById('status').innerHTML='Downloading: '+d.received+'/'+d.total}"
      "else if(d.type=='page_complete'){document.getElementById('status').innerHTML='<a href=/page style=color:#07f>View Page</a> ('+d.size+' bytes)'}"
      "}catch(e){}};"
      "ws.onopen=function(){ws.send('list');ws.send('companions')};"
      "</script></body></html>");
  });
  
  // Serve the received page with mesh:// links rewritten
  server.on("/page", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (currentPage.length() > 0) {
      // Rewrite all mesh:// links to /mesh?node=X&page=Y format
      String modifiedPage = currentPage;
      int pos = 0;
      while ((pos = modifiedPage.indexOf("mesh://", pos)) != -1) {
        // Find the end of the URL (look for quote)
        int endPos = modifiedPage.indexOf('"', pos);
        if (endPos == -1) endPos = modifiedPage.indexOf('\'', pos);
        if (endPos == -1) break;
        
        // Extract mesh://nodeId/page
        String meshUrl = modifiedPage.substring(pos + 7, endPos);  // Skip "mesh://"
        int slashPos = meshUrl.indexOf('/');
        if (slashPos > 0) {
          String nodeId = meshUrl.substring(0, slashPos);
          String pagePath = meshUrl.substring(slashPos);
          
          // Replace with /meshgo?node=X&page=Y (meshgo triggers immediate request)
          String newUrl = "/meshgo?node=" + nodeId + "&page=" + pagePath;
          modifiedPage = modifiedPage.substring(0, pos) + newUrl + modifiedPage.substring(endPos);
          pos += newUrl.length();
        } else {
          pos = endPos;
        }
      }
      request->send(200, "text/html", modifiedPage);
    } else {
      request->send(404, "text/plain", "No page loaded yet");
    }
  });
  
  // Handle mesh link requests with auto-redirect
  server.on("/meshgo", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (request->hasParam("node") && request->hasParam("page")) {
      String nodeIdStr = request->getParam("node")->value();
      String pagePath = request->getParam("page")->value();
      
      Serial.printf("[MESHGO] Node: %s, Page: %s\n", nodeIdStr.c_str(), pagePath.c_str());
      
      // Find node by ID string
      bool found = false;
      String nodeName = "Unknown";
      Serial.printf("[MESHGO] Searching for node: %s\n", nodeIdStr.c_str());
      Serial.printf("[MESHGO] Discovered nodes: %d\n", discoveredNodes.size());
      for (size_t i = 0; i < discoveredNodes.size(); i++) {
        char hexbuf[16];
        sprintf(hexbuf, "%02x%02x%02x%02x", 
                discoveredNodes[i].node_id[0],
                discoveredNodes[i].node_id[1],
                discoveredNodes[i].node_id[2],
                discoveredNodes[i].node_id[3]);
        String nodeId = String(hexbuf);
        Serial.printf("[MESHGO] Comparing with: %s (%s)\n", nodeId.c_str(), discoveredNodes[i].name);
        String searchId = nodeIdStr;
        searchId.toLowerCase();
        if (nodeId == searchId) {
          nodeName = String(discoveredNodes[i].name);
          requestPage(discoveredNodes[i].node_id, pagePath.c_str());
          Serial.printf("[MESHGO] Requesting from %s\n", nodeName.c_str());
          found = true;
          break;
        }
      }
      
      if (found) {
        // Show loading page with WebSocket progress tracking
        String html = "<html><head><style>";
        html += "body{background:#111;color:#fff;font-family:Arial;text-align:center;padding:50px}";
        html += "h1{color:#0077ff}";
        html += ".progress-bar{width:80%;max-width:400px;height:30px;background:#333;border-radius:5px;overflow:hidden;margin:20px auto}";
        html += ".progress-fill{height:100%;background:#0077ff;transition:width 0.3s;text-align:center;line-height:30px;color:#fff;width:0%}";
        html += ".status{color:#888;margin-top:10px}";
        html += "</style></head><body>";
        html += "<h1>📡 Loading from " + nodeName + "</h1>";
        html += "<p>Requesting: <b>" + pagePath + "</b></p>";
        html += "<div class='progress-bar'><div class='progress-fill' id='pbar'>Connecting...</div></div>";
        html += "<p class='status' id='status'>Sending request over LoRa...</p>";
        html += "<script>";
        html += "var ws=new WebSocket('ws://'+location.host+'/ws');";
        html += "ws.onmessage=function(e){try{var d=JSON.parse(e.data);";
        html += "if(d.type=='page_start'){document.getElementById('pbar').style.width='5%';document.getElementById('pbar').innerText='Starting...';document.getElementById('status').innerText='Receiving data...';}";
        html += "else if(d.type=='progress'){var pct=Math.round((d.received/d.total)*100);document.getElementById('pbar').style.width=pct+'%';document.getElementById('pbar').innerText=d.received+'/'+d.total+' ('+pct+'%)';document.getElementById('status').innerText='Downloading page chunks...';}";
        html += "else if(d.type=='page_complete'){document.getElementById('pbar').style.width='100%';document.getElementById('pbar').innerText='Complete!';document.getElementById('status').innerText='Redirecting...';setTimeout(function(){location.href='/page'},500);}}catch(e){}}";
        html += "</script></body></html>";
        request->send(200, "text/html", html);
      } else {
        Serial.println("[MESHGO] Node not found");
        request->send(404, "text/plain", "Node not found: " + nodeIdStr);
      }
    } else {
      request->send(400, "text/plain", "Missing node or page parameter");
    }
  });
  
  ws.onEvent(onWebSocketEvent);
  server.addHandler(&ws);
  server.begin();
  Serial.println("Web server started");
}
#endif  // USB_ONLY

// Process command from either Serial or USB host
void processCommand(String cmd, Stream* output) {
  cmd.trim();
  cmd.replace("\r", "");
  
  // Strip leading control characters
  while (cmd.length() > 0 && cmd[0] < 0x20) {
    cmd = cmd.substring(1);
  }
  
  if (cmd.length() == 0) return;
  
  output->printf("[CMD] Received: '%s' (len=%d)\n", cmd.c_str(), cmd.length());
  
  const char* cmdStr = cmd.c_str();
  
  if (strcmp(cmdStr, "list") == 0) {
    // Send node list to all clients
    String nodesJson = "{\"type\":\"nodes\",\"nodes\":[";
    for (size_t i = 0; i < discoveredNodes.size(); i++) {
      auto &node = discoveredNodes[i];
      if (i > 0) nodesJson += ",";
      nodesJson += "{\"index\":" + String(i) + ",";
      nodesJson += "\"name\":\"" + String(node.name) + "\",";
      nodesJson += "\"id\":\"" + String(node.node_id[0], HEX) + String(node.node_id[1], HEX) + String(node.node_id[2], HEX) + String(node.node_id[3], HEX) + "\",";
      nodesJson += "\"pages\":[";
      for (int p = 0; p < node.page_count; p++) {
        if (p > 0) nodesJson += ",";
        nodesJson += "\"" + String(node.pages[p]) + "\"";
      }
      nodesJson += "]}";
    }
    nodesJson += "]}";
    sendEvent(nodesJson);
    output->println("Node list sent to clients");
    
  } else if (strncmp(cmdStr, "req ", 4) == 0) {
    int firstSpace = cmd.indexOf(' ');
    int secondSpace = cmd.indexOf(' ', firstSpace + 1);
    
    if (secondSpace > 0) {
      int nodeIndex = cmd.substring(firstSpace + 1, secondSpace).toInt();
      String pagePath = cmd.substring(secondSpace + 1);
      
      // Add leading slash if missing
      if (pagePath.length() > 0 && pagePath[0] != '/') {
        pagePath = "/" + pagePath;
      }
      
      if (nodeIndex >= 0 && nodeIndex < (int)discoveredNodes.size()) {
        output->printf("Requesting %s from %s (index %d)\n", pagePath.c_str(), discoveredNodes[nodeIndex].name, nodeIndex);
        requestPage(discoveredNodes[nodeIndex].node_id, pagePath.c_str());
        output->println("LoRa request sent, waiting for response...");
      } else {
        output->printf("ERROR:Invalid node index %d (have %d nodes)\n", nodeIndex, discoveredNodes.size());
      }
    } else {
      output->println("ERROR:Usage: req <node_index> <page>");
    }
    
  } else if (strncmp(cmdStr, "msg ", 4) == 0) {
    // Broadcast message: msg <text>
    String message = cmd.substring(4);
    if (message.length() > 0) {
      sendCompanionMessage(nullptr, message.c_str());  // nullptr = broadcast
      output->println("Message broadcast sent");
    } else {
      output->println("ERROR:Usage: msg <message>");
    }
    
  } else if (strncmp(cmdStr, "dm ", 3) == 0) {
    // Direct message: dm <companion_index> <text>
    int firstSpace = cmd.indexOf(' ', 3);
    if (firstSpace > 0) {
      int compIndex = cmd.substring(3, firstSpace).toInt();
      String message = cmd.substring(firstSpace + 1);
      
      if (compIndex >= 0 && compIndex < (int)discoveredCompanions.size() && message.length() > 0) {
        sendCompanionMessage(discoveredCompanions[compIndex].node_id, message.c_str());
        output->println("Direct message sent");
      } else {
        output->println("ERROR:Invalid companion index or empty message");
      }
    } else {
      output->println("ERROR:Usage: dm <companion_index> <message>");
    }
    
  } else if (strcmp(cmdStr, "companions") == 0) {
    // Send companion list to all clients
    String json = "{\"type\":\"companions\",\"companions\":[";
    for (size_t i = 0; i < discoveredCompanions.size(); i++) {
      auto &c = discoveredCompanions[i];
      if (i > 0) json += ",";
      char idStr[16];
      sprintf(idStr, "%02x%02x%02x%02x", c.node_id[0], c.node_id[1], c.node_id[2], c.node_id[3]);
      json += "{\"index\":" + String(i) + ",\"name\":\"" + String(c.name) + "\",\"id\":\"" + String(idStr) + "\",\"status\":" + String(c.status) + "}";
    }
    json += "]}";
    sendEvent(json);
    output->println("Companion list sent to clients");
    
  } else if (strcmp(cmdStr, "getpage") == 0) {
    // Send current page content over serial (base64 encoded, chunked)
    if (currentPage.length() == 0) {
      output->println("ERROR:No page loaded");
    } else {
      output->println("PAGE_START:");
      const uint8_t* pageData = (const uint8_t*)currentPage.c_str();
      size_t pageLen = currentPage.length();
      size_t chunkSize = 180;  // ~240 base64 chars per line
      for (size_t offset = 0; offset < pageLen; offset += chunkSize) {
        size_t remaining = pageLen - offset;
        size_t thisChunk = (remaining < chunkSize) ? remaining : chunkSize;
        String encoded = base64_encode(pageData + offset, thisChunk);
        output->println("PAGE_LINE:" + encoded);
      }
      output->println("PAGE_END:");
      output->printf("Sent %d bytes as base64\n", pageLen);
    }
    
  } else if (strncmp(cmdStr, "meshgo ", 7) == 0) {
    // Request page by node ID: meshgo <hex_node_id> <page_path>
    int firstSpace = cmd.indexOf(' ', 7);
    if (firstSpace > 0) {
      String nodeIdStr = cmd.substring(7, firstSpace);
      String pagePath = cmd.substring(firstSpace + 1);
      
      // Add leading slash if missing
      if (pagePath.length() > 0 && pagePath[0] != '/') {
        pagePath = "/" + pagePath;
      }
      
      nodeIdStr.toLowerCase();
      bool found = false;
      for (size_t i = 0; i < discoveredNodes.size(); i++) {
        char hexbuf[16];
        sprintf(hexbuf, "%02x%02x%02x%02x",
                discoveredNodes[i].node_id[0],
                discoveredNodes[i].node_id[1],
                discoveredNodes[i].node_id[2],
                discoveredNodes[i].node_id[3]);
        if (nodeIdStr == String(hexbuf)) {
          requestPage(discoveredNodes[i].node_id, pagePath.c_str());
          output->printf("Requesting %s from %s\n", pagePath.c_str(), discoveredNodes[i].name);
          found = true;
          break;
        }
      }
      if (!found) {
        output->println("ERROR:Node not found: " + nodeIdStr);
      }
    } else {
      output->println("ERROR:Usage: meshgo <node_id> <page>");
    }
    
  } else if (strcmp(cmdStr, "help") == 0) {
    output->println("HELP:list - Show discovered nodes");
    output->println("HELP:req <node_index> <page> - Request a page");
    output->println("HELP:companions - Show discovered companions");
    output->println("HELP:msg <text> - Broadcast message to all companions");
    output->println("HELP:dm <index> <text> - Direct message to companion");
    output->println("HELP:getpage - Get loaded page content (base64)");
    output->println("HELP:meshgo <node_id> <page> - Request page by node ID");
    
  } else if (cmd.length() > 0) {
    output->println("ERROR:Unknown command. Type 'help' for commands.");
  }
}

void setup() {
  Serial.begin(115200);
  delay(2000);
  
  Serial.println("\n\n");
  Serial.println("╔═══════════════════════════════════════════════╗");
#ifdef USB_ONLY
  Serial.println("║   MeshWeb Companion - USB Only Mode          ║");
#elif defined(BLE_ONLY)
  Serial.println("║   MeshWeb Companion - BLE Mode               ║");
#else
  Serial.println("║   MeshWeb Companion - ESP32 Xiao             ║");
#endif
  Serial.println("║   Decentralized Web Over LoRa                ║");
  Serial.println("╚═══════════════════════════════════════════════╝");
  
  // Generate node ID from chip eFuse MAC (works with or without WiFi)
  uint64_t efuse = ESP.getEfuseMac();
  uint8_t mac[6];
  memcpy(mac, &efuse, 6);
  // Use last 4 bytes as node ID
  node_id[0] = mac[2];
  node_id[1] = mac[3];
  node_id[2] = mac[4];
  node_id[3] = mac[5];
  
  // Generate companion friendly name
  String friendlyName = generateNodeName(node_id);
  strncpy(companionName, friendlyName.c_str(), 31);
  companionName[31] = '\0';
  
  Serial.print("\nCompanion: ");
  Serial.println(companionName);
  Serial.print("Node ID: ");
  for (int i = 0; i < 4; i++) {
    Serial.printf("%02X", node_id[i]);
  }
  Serial.println();
  Serial.printf("(Derived from MAC: %02X:%02X:%02X:%02X:%02X:%02X)\n",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  
#ifndef USB_ONLY
  // Initialize WiFi AP with unique name
  WiFi.mode(WIFI_AP);
  String apName = String(WIFI_SSID) + " - " + String(companionName);
  WiFi.softAP(apName.c_str(), WIFI_PASSWORD);
  IPAddress IP = WiFi.softAPIP();
  Serial.print("WiFi AP IP: ");
  Serial.println(IP);
  Serial.printf("WiFi AP '%s' started\n", apName.c_str());
#else
  Serial.println("WiFi disabled (USB_ONLY mode)");
#endif
  
  // Initialize LoRa
  setupLoRa();
  
#ifndef USB_ONLY
  // Setup web server
  setupWebServer();
#endif
  
#ifdef ENABLE_BLE
  setupBLE();
#endif
  
  if (loraReady) {
    Serial.println("\n✓ Setup complete - listening for web nodes...");
#ifndef USB_ONLY
    Serial.printf("Browse to: http://%s\n", WiFi.softAPIP().toString().c_str());
#endif
    Serial.println("\nSerial Commands:");
    Serial.println("  'list' - Show discovered nodes");
    Serial.println("  'req <index> <page>' - Request a page");
    Serial.println("  'companions' - Show discovered companions");
    Serial.println("  'msg <text>' - Broadcast message");
    Serial.println("  'dm <index> <text>' - Direct message");
    Serial.println("  'getpage' - Get loaded page (base64)");
    Serial.println("  'meshgo <node_id> <page>' - Request by node ID\n");
    
    // Send initial companion announce
    delay(1000);
    sendCompanionAnnounce();
    lastCompanionAnnounce = millis();
  }
}

void loop() {
  // Check for received LoRa packets
  if (receivedFlag) {
    receivedFlag = false;
    handleLoRaPacket();
  }
  
  // Check for request timeout
  if (requestPending && (millis() - requestTime > REQUEST_TIMEOUT)) {
    Serial.println("⚠ Request timeout - no response received");
    requestPending = false;
  }
  
  // Check for serial commands
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    processCommand(cmd, &Serial);
  }
  
#ifndef USB_ONLY
  // Clean up WebSocket clients
  ws.cleanupClients();
#endif
  
  // Periodic companion announce (suppress during page transfer to avoid collisions)
  if (loraReady && !receivingPage && (millis() - lastCompanionAnnounce > COMPANION_ANNOUNCE_INTERVAL)) {
    sendCompanionAnnounce();
    lastCompanionAnnounce = millis();
  }
  
  // Detect stalled page transfer (chunk lost due to collision)
  if (receivingPage && lastChunkTime > 0 && (millis() - lastChunkTime > 3000)) {
    Serial.printf("⚠ Page transfer stalled at %d/%d chunks\n", receivedChunks, totalChunksExpected);
    Serial.printf("   Delivering partial page (%d bytes)\n", currentPage.length());
    
    // Deliver what we have
    sendEvent("{\"type\":\"page_complete\",\"size\":" + String(currentPage.length()) + "}");
    
#ifdef ENABLE_BLE
    bleAutoSendPage = true;
#endif
    receivingPage = false;
    requestPending = false;
    currentRequestId = 0;
    lastChunkTime = 0;
  }
  
#ifdef ENABLE_BLE
  // Process queued BLE commands (outside NimBLE callback for reliable notifications)
  if (pendingBLECommand.length() > 0) {
    String cmd = pendingBLECommand;
    pendingBLECommand = "";
    processCommand(cmd, &bleOutputStream);
  }
  
  // Auto-send page data over BLE after page_complete
  if (bleAutoSendPage) {
    bleAutoSendPage = false;
    if (bleClientConnected && currentPage.length() > 0) {
      Serial.println("[BLE] Auto-sending page data...");
      bleOutputStream.println("PAGE_START:");
      const uint8_t* pageData = (const uint8_t*)currentPage.c_str();
      size_t pageLen = currentPage.length();
      size_t chunkSize = 180;
      for (size_t offset = 0; offset < pageLen; offset += chunkSize) {
        size_t remaining = pageLen - offset;
        size_t thisChunk = (remaining < chunkSize) ? remaining : chunkSize;
        String encoded = base64_encode(pageData + offset, thisChunk);
        bleOutputStream.println("PAGE_LINE:" + encoded);
      }
      bleOutputStream.println("PAGE_END:");
      Serial.printf("[BLE] Auto-sent %d bytes as base64\n", pageLen);
    }
  }
#endif
  
  delay(10);
}
