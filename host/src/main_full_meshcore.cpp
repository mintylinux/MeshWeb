// MeshCore Web Node - Integrated Version
// Hosts web content accessible via WiFi AND LoRa mesh network

#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>

// MeshCore includes
#include <Mesh.h>
#include <helpers/ArduinoHelpers.h>
#include <helpers/SimpleMeshTables.h>
#include <helpers/IdentityStore.h>
#include <helpers/StaticPoolPacketManager.h>
#include <target.h>

// Web protocol
#include "WebProtocol.h"

// Global objects
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// MeshCore objects
StdRNG fast_rng;
SimpleMeshTables tables;
mesh::Mesh* the_mesh = nullptr;
mesh::Identity node_identity;
uint8_t node_id[4]; // First 4 bytes of public key

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

// Forward declarations
void handlePageRequest(const uint8_t* data, int len, const uint8_t* sender);
void sendPageAnnounce();
void sendPageData(const char* path, uint8_t request_id, const uint8_t* recipient);

// WebSocket event handler (same as before)
void onWebSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, 
                      AwsEventType type, void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    Serial.printf("WebSocket client #%u connected\\n", client->id());
    
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
    Serial.printf("WebSocket client #%u disconnected\\n", client->id());
    
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
          
          Serial.printf("Message via WiFi: %s\\n", msg.content.c_str());
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
  
  ws.onEvent(onWebSocketEvent);
  server.addHandler(&ws);
  
  server.begin();
  Serial.println("Web server started");
}

void setupWiFi() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid, password);
  
  IPAddress IP = WiFi.softAPIP();
  Serial.print("AP IP: ");
  Serial.println(IP);
  Serial.printf("WiFi AP '%s' started\\n", ssid);
}

// MeshCore packet handler for custom web protocol packets
void onRawCustomPacket(mesh::Packet* packet) {
  if (packet->payload_len < 1) return;
  
  uint8_t msg_type = WebProtocol::getMessageType(packet->payload);
  
  Serial.printf("Received custom packet, type: 0x%02X, len: %d\\n", 
                msg_type, packet->payload_len);
  
  switch (msg_type) {
    case WEB_MSG_PAGE_REQUEST: {
      WebPageRequest req;
      if (req.readFrom(packet->payload, packet->payload_len)) {
        // Check if this request is for us
        if (WebProtocol::isTargetNode(req.target_node, node_id)) {
          Serial.printf("Page request for: %s\\n", req.page_path);
          
          // TODO: Get sender identity from packet to send response
          // For now, we'll just log it
          // sendPageData(req.page_path, req.request_id, sender_id);
        }
      }
      break;
    }
    
    case WEB_MSG_PAGE_ANNOUNCE: {
      WebPageAnnounce announce;
      if (announce.readFrom(packet->payload, packet->payload_len)) {
        Serial.printf("Page announce from node: %02X%02X%02X%02X\\n",
                     announce.node_id[0], announce.node_id[1],
                     announce.node_id[2], announce.node_id[3]);
        Serial.printf("Pages: %d\\n", announce.page_count);
        for (int i = 0; i < announce.page_count; i++) {
          Serial.printf("  - %s\\n", announce.pages[i]);
        }
      }
      break;
    }
    
    case WEB_MSG_PAGE_DATA: {
      WebPageData data;
      if (data.readFrom(packet->payload, packet->payload_len)) {
        Serial.printf("Page data chunk %d/%d, %d bytes\\n",
                     data.chunk_index, data.total_chunks, data.data_len);
        // TODO: Reassemble chunks and display/forward to WiFi clients
      }
      break;
    }
  }
}

// Send page announcement broadcast
void sendPageAnnounce() {
  WebPageAnnounce announce;
  announce.msg_type = WEB_MSG_PAGE_ANNOUNCE;
  memcpy(announce.node_id, node_id, 4);
  announce.timestamp = millis();
  
  // List available pages from SPIFFS
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
  
  Serial.printf("Sending page announce: %d pages\\n", announce.page_count);
  
  // TODO: Send as broadcast flood packet
  // For now just log
  // the_mesh->sendFloodPacket(PAYLOAD_TYPE_RAW_CUSTOM, buf, len);
}

// Send page data in response to request
void sendPageData(const char* path, uint8_t request_id, const uint8_t* recipient) {
  File file = SPIFFS.open(path, "r");
  if (!file) {
    Serial.printf("File not found: %s\\n", path);
    return;
  }
  
  size_t fileSize = file.size();
  uint16_t total_chunks = (fileSize + WEB_MAX_CHUNK_SIZE - 1) / WEB_MAX_CHUNK_SIZE;
  
  Serial.printf("Sending file %s: %d bytes in %d chunks\\n", 
               path, fileSize, total_chunks);
  
  // Send chunks
  for (uint16_t chunk = 0; chunk < total_chunks; chunk++) {
    WebPageData data;
    data.msg_type = WEB_MSG_PAGE_DATA;
    data.request_id = request_id;
    data.chunk_index = chunk;
    data.total_chunks = total_chunks;
    
    data.data_len = file.read(data.data, WEB_MAX_CHUNK_SIZE);
    
    uint8_t buf[256];
    int len = data.writeTo(buf);
    
    // TODO: Send as direct packet to recipient
    // the_mesh->sendDirectPacket(recipient, PAYLOAD_TYPE_RAW_CUSTOM, buf, len);
    
    Serial.printf("Sent chunk %d/%d\\n", chunk+1, total_chunks);
    delay(100); // Small delay between chunks
  }
  
  file.close();
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\\n\\n=== MeshCore Web Node Starting ===");
  
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
  
  // Initialize LoRa radio
  Serial.println("Initializing LoRa...");
  if (!radio_init()) {
    Serial.println("LoRa init failed!");
  } else {
    Serial.println("LoRa initialized");
  }
  
  // Initialize RNG
  fast_rng.begin(radio_get_rng_seed());
  
  // Load or create identity
  IdentityStore store(SPIFFS, "/identity");
  if (!store.load("_main", node_identity)) {
    Serial.println("Generating new identity...");
    node_identity = radio_new_identity();
    store.save("_main", node_identity);
  }
  
  // Extract node ID (first 4 bytes of public key)
  WebProtocol::extractNodeId(node_id, node_identity.pub_key);
  
  Serial.print("Node ID: ");
  for (int i = 0; i < 4; i++) {
    Serial.printf("%02X", node_id[i]);
  }
  Serial.println();
  
  // TODO: Initialize MeshCore Mesh object
  // This requires creating a custom Mesh subclass to handle our web protocol
  // For now, we have the infrastructure ready
  
  Serial.println("=== Setup Complete ===");
  Serial.println("System ready - WiFi + LoRa active");
  
  // Send initial page announce
  sendPageAnnounce();
  last_announce = millis();
}

void loop() {
  // Clean up WebSocket clients
  ws.cleanupClients();
  
  // TODO: Call mesh loop when mesh is initialized
  // if (the_mesh) {
  //   the_mesh->loop();
  // }
  
  // Periodic page announce
  if (millis() - last_announce > ANNOUNCE_INTERVAL) {
    sendPageAnnounce();
    last_announce = millis();
  }
  
  delay(10);
}
