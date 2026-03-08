// MeshWeb Host Node - RadioLib Version
// WiFi web server + LoRa broadcasts for decentralized web

#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <RadioLib.h>

// Web protocol
#include "WebProtocol.h"

// LoRa radio (Heltec V3 pins)
#define LORA_SCK 9
#define LORA_MISO 11
#define LORA_MOSI 10
#define LORA_CS 8
#define LORA_RST 12
#define LORA_DIO1 14
#define LORA_BUSY 13

SX1262 radio = new Module(LORA_CS, LORA_DIO1, LORA_RST, LORA_BUSY);

// Global objects
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// Node identity (simplified - just random ID)
uint8_t node_id[4];
char node_name[32];

// Generate a friendly name from node ID
String generateNodeName(uint8_t* id) {
  const char* adjectives[] = {"Red", "Blue", "Green", "Swift", "Bright", "Dark", "Wild", "Calm"};
  const char* nouns[] = {"Fox", "Wolf", "Hawk", "Bear", "Eagle", "Tiger", "Lion", "Raven"};
  
  // Use node ID bytes to pick words
  int adjIdx = id[0] % 8;
  int nounIdx = id[1] % 8;
  
  return String(adjectives[adjIdx]) + " " + String(nouns[nounIdx]);
}

// Page announce timer
unsigned long last_announce = 0;
const unsigned long ANNOUNCE_INTERVAL = 60000; // Every 60 seconds

// WiFi credentials
const char* ssid = WIFI_SSID;
const char* password = WIFI_PASSWORD;

// Message storage
struct Message {
  String id;
  String author;
  String content;
  unsigned long timestamp;
};
std::vector<Message> messages;

// Radio state
bool loraReady = false;
volatile bool receivedFlag = false;
bool inFSKMode = false;  // Track current mode

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
    11,         // Spreading Factor: 11
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

// WebSocket handler (same as before)
void onWebSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, 
                      AwsEventType type, void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    Serial.printf("WebSocket client #%u connected\n", client->id());
    
    JsonDocument doc;
    doc["type"] = "init";
    JsonArray msgs = doc["messages"].to<JsonArray>();
    
    for (auto &msg : messages) {
      JsonObject msgObj = msgs.add<JsonObject>();
      msgObj["id"] = msg.id;
      msgObj["author"] = msg.author;
      msgObj["content"] = msg.content;
      msgObj["timestamp"] = msg.timestamp;
    }
    
    String response;
    serializeJson(doc, response);
    client->text(response);
    
  } else if (type == WS_EVT_DISCONNECT) {
    Serial.printf("WebSocket client #%u disconnected\n", client->id());
    
  } else if (type == WS_EVT_DATA) {
    AwsFrameInfo *info = (AwsFrameInfo*)arg;
    if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
      data[len] = 0;
      
      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, (char*)data);
      
      if (!error) {
        String type = doc["type"];
        
        if (type == "post") {
          Message msg;
          msg.id = String(millis());
          msg.author = doc["author"] | "Anonymous";
          msg.content = doc["content"] | "";
          msg.timestamp = millis();
          
          messages.push_back(msg);
          
          JsonDocument response;
          response["type"] = "newMessage";
          response["id"] = msg.id;
          response["author"] = msg.author;
          response["content"] = msg.content;
          response["timestamp"] = msg.timestamp;
          
          String responseStr;
          serializeJson(response, responseStr);
          ws.textAll(responseStr);
          
          Serial.printf("Message via WiFi: %s\n", msg.content.c_str());
        }
      }
    }
  }
}

void setupWebServer() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(SPIFFS, "/index.html", "text/html");
  });
  
  server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(SPIFFS, "/style.css", "text/css");
  });
  
  server.on("/app.js", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(SPIFFS, "/app.js", "text/javascript");
  });
  
  server.on("/api/messages", HTTP_GET, [](AsyncWebServerRequest *request) {
    JsonDocument doc;
    JsonArray msgs = doc["messages"].to<JsonArray>();
    
    for (auto &msg : messages) {
      JsonObject msgObj = msgs.add<JsonObject>();
      msgObj["id"] = msg.id;
      msgObj["author"] = msg.author;
      msgObj["content"] = msg.content;
      msgObj["timestamp"] = msg.timestamp;
    }
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });
  
  // File upload page
  server.on("/upload", HTTP_GET, [](AsyncWebServerRequest *request) {
    String html = "<html><head><title>MeshWeb File Manager</title><style>body{font-family:Arial;max-width:800px;margin:50px auto;background:#111;color:#fff}";
    html += "h1{color:#07f}form{background:#222;padding:20px;border-radius:8px;margin:20px 0}input[type=file]{margin:10px 0}";
    html += "button{background:#07f;color:#fff;border:none;padding:10px 20px;cursor:pointer;border-radius:4px;margin:5px}";
    html += "button:hover{background:#05d}button.del{background:#c00}button.del:hover{background:#900}";
    html += ".file-list{background:#222;padding:20px;border-radius:8px}.file{padding:10px;border-bottom:1px solid #333;display:flex;justify-content:space-between;align-items:center}";
    html += ".file-info{flex:1}</style></head>";
    html += "<body><h1>📁 MeshWeb File Manager</h1>";
    html += "<form method='POST' action='/upload' enctype='multipart/form-data'>";
    html += "<h3>Upload HTML File</h3>";
    html += "<input type='file' name='file' accept='.html,.js,.css' required><br>";
    html += "<button type='submit'>Upload</button></form>";
    html += "<h3>Current Files</h3><div class='file-list'>";
    File root = SPIFFS.open("/");
    File file = root.openNextFile();
    while (file) {
      String fname = String(file.name());
      html += "<div class='file'><span class='file-info'>📄 " + fname + " (" + String(file.size()) + " bytes)</span>";
      html += "<button class='del' onclick=\"if(confirm('Delete " + fname + "?'))location.href='/delete?file=" + fname + "'\">Delete</button></div>";
      file = root.openNextFile();
    }
    html += "</div><br><a href='/'><button>Back to Home</button></a></body></html>";
    request->send(200, "text/html", html);
  });
  
  // Handle file delete
  server.on("/delete", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (request->hasParam("file")) {
      String filename = request->getParam("file")->value();
      // Ensure path starts with /
      if (!filename.startsWith("/")) {
        filename = "/" + filename;
      }
      
      if (SPIFFS.exists(filename)) {
        SPIFFS.remove(filename);
        Serial.printf("Deleted file: %s\n", filename.c_str());
        request->redirect("/upload");
      } else {
        request->send(404, "text/plain", "File not found: " + filename);
      }
    } else {
      request->send(400, "text/plain", "Missing file parameter");
    }
  });
  
  // Handle file upload
  server.on("/upload", HTTP_POST, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html", "<html><body><h2>Upload complete!</h2><a href='/upload'>Back</a></body></html>");
  }, [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
    static File uploadFile;
    if (index == 0) {
      Serial.printf("Upload started: %s\n", filename.c_str());
      String path = "/" + filename;
      uploadFile = SPIFFS.open(path, "w");
    }
    if (uploadFile) {
      uploadFile.write(data, len);
    }
    if (final) {
      uploadFile.close();
      Serial.printf("Upload complete: %s (%d bytes)\n", filename.c_str(), index + len);
    }
  });
  
  ws.onEvent(onWebSocketEvent);
  server.addHandler(&ws);
  
  server.begin();
  Serial.println("Web server started");
}

void setupWiFi() {
  WiFi.mode(WIFI_AP);
  
  // Create SSID with node name
  String fullSSID = String(ssid) + " - " + String(node_name);
  WiFi.softAP(fullSSID.c_str(), password);
  
  IPAddress IP = WiFi.softAPIP();
  Serial.print("AP IP: ");
  Serial.println(IP);
  Serial.printf("WiFi AP '%s' started\n", fullSSID.c_str());
}

void setupLoRa() {
  Serial.println("\nInitializing LoRa...");
  
  // Initialize SPI
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
  
  // Initialize radio
  int state = radio.begin(915.0,   // Frequency: 915.0 MHz (US ISM band 902-928 MHz)
                         250.0,     // Bandwidth: 250 kHz
                         9,         // Spreading Factor: 9 (balance of speed and reliability)
                         5,         // Coding Rate: 4/5
                         0x12,      // Sync Word (private network)
                         22,        // Output power: 22 dBm
                         8,         // Preamble length
                         1.8);      // TCXO voltage for Heltec V3
  
  if (state == RADIOLIB_ERR_NONE) {
    Serial.println("LoRa init success!");
    Serial.println("Frequency: 915.0 MHz (US ISM Band - MeshWeb Network)");
    Serial.println("This is SEPARATE from main MeshCore network");
    loraReady = true;
    Serial.println("✓ MeshWeb network active");
    
    // Set interrupt for receive
    radio.setDio1Action(setFlag);
    
    // Start listening
    radio.startReceive();
    
  } else {
    Serial.print("LoRa init failed, code: ");
    Serial.println(state);
    loraReady = false;
  }
}

// Send page data in response to request
void sendPageData(uint8_t request_id, const char* page_path) {
  if (!loraReady) return;
  
  // Map / to /index.html
  String path = String(page_path);
  if (path == "/") path = "/index.html";
  
  Serial.printf("\n>>> Sending page: %s (requested: %s)\n", path.c_str(), page_path);
  
  // Stay in SF9 mode - no mode switching for reliability
  
  // Open file
  File file = SPIFFS.open(path.c_str(), "r");
  if (!file) {
    Serial.println("✗ File not found!");
    switchToLoRa();  // Switch back even on error
    return;
  }
  
  size_t fileSize = file.size();
  Serial.printf("File size: %d bytes\n", fileSize);
  
  // Calculate chunks
  int totalChunks = (fileSize + WEB_MAX_CHUNK_SIZE - 1) / WEB_MAX_CHUNK_SIZE;
  Serial.printf("Total chunks: %d (SF9 mode)\n", totalChunks);
  
  // Send chunks
  for (int chunk = 0; chunk < totalChunks; chunk++) {
    WebPageData data;
    data.msg_type = WEB_MSG_PAGE_DATA;
    data.request_id = request_id;
    data.chunk_index = chunk;
    data.total_chunks = totalChunks;
    
    // Read chunk
    data.data_len = file.read(data.data, WEB_MAX_CHUNK_SIZE);
    
    uint8_t buf[256];
    int len = data.writeTo(buf);
    
    Serial.printf("  Sending chunk %d/%d (%d bytes)...\n", 
                  chunk + 1, totalChunks, data.data_len);
    
    // Send chunk
    int state = radio.transmit(buf, len);
    
    if (state == RADIOLIB_ERR_NONE) {
      Serial.println("    ✓ Sent");
    } else {
      Serial.printf("    ✗ Failed, code: %d\n", state);
      break;
    }
    
    // Delay to allow receiver to process, send BLE events, and return to RX mode
    delay(200);
  }
  
  file.close();
  Serial.println("✓ Page transmission complete");
  
  // Return to receive mode (already in SF9)
  radio.startReceive();
}

// Send page announcement over LoRa
void sendPageAnnounce() {
  if (!loraReady) return;
  
  WebPageAnnounce announce;
  announce.msg_type = WEB_MSG_PAGE_ANNOUNCE;
  memcpy(announce.node_id, node_id, 4);
  strncpy(announce.node_name, node_name, 31);
  announce.node_name[31] = '\0';
  announce.timestamp = millis();
  
  // List available pages
  File root = SPIFFS.open("/");
  File file = root.openNextFile();
  announce.page_count = 0;
  
  while (file && announce.page_count < WEB_MAX_PAGES_ANNOUNCE) {
    if (!file.isDirectory()) {
      String name = String(file.name());
      if (name.endsWith(".html") || name == "/index.html" || 
          name == "/app.js" || name == "/style.css") {
        strncpy(announce.pages[announce.page_count], name.c_str(), WEB_MAX_PATH_LEN);
        announce.page_count++;
      }
    }
    file = root.openNextFile();
  }
  
  uint8_t buf[256];
  int len = announce.writeTo(buf);
  
  Serial.printf("\n=== Broadcasting PAGE_ANNOUNCE ===\n");
  Serial.printf("Node: %s\n", node_name);
  Serial.printf("ID: %02X%02X%02X%02X\n", 
                node_id[0], node_id[1], node_id[2], node_id[3]);
  Serial.printf("Pages: %d\n", announce.page_count);
  for (int i = 0; i < announce.page_count; i++) {
    Serial.printf("  - %s\n", announce.pages[i]);
  }
  
  // Send over LoRa
  int state = radio.transmit(buf, len);
  
  if (state == RADIOLIB_ERR_NONE) {
    Serial.println("Broadcast sent successfully!");
  } else {
    Serial.printf("Broadcast failed, code: %d\n", state);
  }
  
  // Return to receive mode
  radio.startReceive();
}

// Handle received LoRa packet
void handleLoRaPacket() {
  if (!loraReady) return;
  
  uint8_t buf[256];
  int state = radio.readData(buf, 256);
  
  if (state == RADIOLIB_ERR_NONE) {
    int len = radio.getPacketLength();
    float rssi = radio.getRSSI();
    float snr = radio.getSNR();
    
    Serial.printf("\n=== Received LoRa Packet ===\n");
    Serial.printf("Length: %d bytes\n", len);
    Serial.printf("RSSI: %.2f dBm\n", rssi);
    Serial.printf("SNR: %.2f dB\n", snr);
    
    if (len > 0) {
      uint8_t msg_type = WebProtocol::getMessageType(buf);
      Serial.printf("Message Type: 0x%02X\n", msg_type);
      
      switch (msg_type) {
        case WEB_MSG_PAGE_ANNOUNCE: {
          WebPageAnnounce announce;
          if (announce.readFrom(buf, len)) {
            Serial.printf("PAGE_ANNOUNCE from: %02X%02X%02X%02X\n",
                         announce.node_id[0], announce.node_id[1],
                         announce.node_id[2], announce.node_id[3]);
            Serial.printf("Pages: %d\n", announce.page_count);
            for (int i = 0; i < announce.page_count; i++) {
              Serial.printf("  - %s\n", announce.pages[i]);
            }
          }
          break;
        }
        
        case WEB_MSG_PAGE_REQUEST: {
          WebPageRequest req;
          if (req.readFrom(buf, len)) {
            Serial.printf("PAGE_REQUEST for: %s\n", req.page_path);
            Serial.printf("Request ID: %d\n", req.request_id);
            Serial.printf("Target: %02X%02X%02X%02X\n",
                         req.target_node[0], req.target_node[1],
                         req.target_node[2], req.target_node[3]);
            // Check if for us
            if (WebProtocol::isTargetNode(req.target_node, node_id)) {
              Serial.println("✓ Request is for THIS node!");
              // Send the requested page (will switch to fast mode internally)
              sendPageData(req.request_id, req.page_path);
            } else {
              Serial.println("Request is for another node");
            }
          }
          break;
        }
        
        case WEB_MSG_PAGE_DATA: {
          Serial.println("Received PAGE_DATA chunk");
          // TODO: Reassemble and display
          break;
        }
        
        default:
          Serial.println("Unknown message type");
      }
    }
    
  } else if (state == RADIOLIB_ERR_CRC_MISMATCH) {
    Serial.println("CRC error - corrupted packet");
  }
  
  // Return to receive mode
  radio.startReceive();
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n\n╔═══════════════════════════════════════════════╗");
  Serial.println("║   MeshWeb Host - Heltec V3                    ║");
  Serial.println("║   Decentralized Web Over LoRa                 ║");
  Serial.println("╚═══════════════════════════════════════════════╝");
  Serial.println("Frequency: 915.0 MHz (US ISM Band)");
  
  // Generate node ID from MAC address (persistent across reboots)
  uint8_t mac[6];
  WiFi.macAddress(mac);
  // Use last 4 bytes of MAC as node ID
  node_id[0] = mac[2];
  node_id[1] = mac[3];
  node_id[2] = mac[4];
  node_id[3] = mac[5];
  
  // Generate friendly name from ID
  String friendlyName = generateNodeName(node_id);
  strncpy(node_name, friendlyName.c_str(), 31);
  node_name[31] = '\0';
  
  Serial.printf("Node: %s\n", node_name);
  Serial.print("ID: ");
  for (int i = 0; i < 4; i++) {
    Serial.printf("%02X", node_id[i]);
  }
  Serial.println();
  
  // Initialize SPIFFS
  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS Mount Failed");
    return;
  }
  Serial.println("SPIFFS mounted");
  
  // Setup WiFi AP
  setupWiFi();
  
  // Setup web server
  setupWebServer();
  
  // Initialize LoRa
  setupLoRa();
  
  Serial.println("\n=== Setup Complete ===");
  Serial.println("WiFi + LoRa active on isolated network");
  Serial.println("Will broadcast page announcements every 60 seconds");
  
  // Send initial announcement
  if (loraReady) {
    delay(2000);
    sendPageAnnounce();
    last_announce = millis();
  }
}

void loop() {
  // Clean up WebSocket clients
  ws.cleanupClients();
  
  // Check for received LoRa packets
  if (receivedFlag) {
    receivedFlag = false;
    handleLoRaPacket();
  }
  
  // Periodic page announce
  if (loraReady && (millis() - last_announce > ANNOUNCE_INTERVAL)) {
    sendPageAnnounce();
    last_announce = millis();
  }
  
  delay(10);
}
