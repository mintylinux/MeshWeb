#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>

// MeshCore includes (will need proper paths based on your setup)
// For now, this is a placeholder - you'll need to copy or link the MeshCore library

// Global objects
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// MeshCore placeholder (you'll integrate the actual MeshCore library)
struct Message {
  String id;
  String author;
  String content;
  unsigned long timestamp;
};

std::vector<Message> messages;

// WiFi credentials (from platformio.ini)
const char* ssid = WIFI_SSID;
const char* password = WIFI_PASSWORD;

// WebSocket event handler
void onWebSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, 
                      AwsEventType type, void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    Serial.printf("WebSocket client #%u connected from %s\n", 
                  client->id(), client->remoteIP().toString().c_str());
    
    // Send existing messages to new client
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
    // Handle incoming message
    AwsFrameInfo *info = (AwsFrameInfo*)arg;
    if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
      data[len] = 0;
      
      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, (char*)data);
      
      if (!error) {
        String type = doc["type"];
        
        if (type == "post") {
          // Create new message
          Message msg;
          msg.id = String(millis());
          msg.author = doc["author"] | "Anonymous";
          msg.content = doc["content"] | "";
          msg.timestamp = millis();
          
          messages.push_back(msg);
          
          // Broadcast to all clients
          JsonDocument response;
          response["type"] = "newMessage";
          response["id"] = msg.id;
          response["author"] = msg.author;
          response["content"] = msg.content;
          response["timestamp"] = msg.timestamp;
          
          String responseStr;
          serializeJson(response, responseStr);
          ws.textAll(responseStr);
          
          // TODO: Send over MeshCore network to other nodes
          Serial.printf("New message: %s - %s\n", msg.author.c_str(), msg.content.c_str());
        }
      }
    }
  }
}

void setupWebServer() {
  // Serve static files from SPIFFS
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(SPIFFS, "/index.html", "text/html");
  });
  
  server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(SPIFFS, "/style.css", "text/css");
  });
  
  server.on("/app.js", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(SPIFFS, "/app.js", "text/javascript");
  });
  
  // API endpoints
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
  
  // WebSocket
  ws.onEvent(onWebSocketEvent);
  server.addHandler(&ws);
  
  server.begin();
  Serial.println("Web server started");
}

void setupWiFi() {
  // Create WiFi Access Point
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid, password);
  
  IPAddress IP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(IP);
  Serial.printf("WiFi AP '%s' started\n", ssid);
  Serial.printf("Connect and visit http://%s\n", IP.toString().c_str());
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n\n=== MeshCore Web Node Starting ===");
  
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
  
  // TODO: Initialize MeshCore
  // - Setup LoRa radio
  // - Initialize mesh network
  // - Setup repeater functionality
  // - Register callback for incoming mesh messages
  
  Serial.println("=== Setup Complete ===");
  Serial.println("System ready!");
}

void loop() {
  // Clean up WebSocket clients
  ws.cleanupClients();
  
  // TODO: MeshCore loop
  // - Process incoming LoRa packets
  // - Forward messages between nodes
  // - Handle mesh network operations
  
  delay(10);
}
