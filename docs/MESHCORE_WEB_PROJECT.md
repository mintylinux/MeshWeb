# MeshCore Text Internet Project

A complete system for hosting and browsing a text-based internet over LoRa mesh networks using MeshCore.

## 📦 Project Components

### 1. **meshcore-web-node/** - ESP32 Firmware
The firmware that runs on your T-Beam or other ESP32 LoRa devices.

**Features:**
- MeshCore repeater functionality (forwards packets over LoRa)
- WiFi Access Point
- Web server hosting HTML interface
- WebSocket server for real-time communication
- SPIFFS storage for web files

**Status:** ⚠️ Starter template - needs MeshCore integration

**Location:** `/home/chuck/Desktop/meshcore-web-node/`

### 2. **meshcore_web_browser/** - Flutter Desktop App
Cross-platform desktop application for browsing the mesh network.

**Features:**
- Connects to MeshCore nodes via WiFi
- Real-time message viewing
- Post messages to the mesh
- Clean Material Design 3 UI
- Runs on Linux, Windows, macOS

**Status:** ✅ Functional - ready to use

**Location:** `/home/chuck/Desktop/meshcore_web_browser/`

## 🚀 Quick Start

### Step 1: Setup Firmware (ESP32)

```bash
cd /home/chuck/Desktop/meshcore-web-node

# Open in PlatformIO/VS Code
# Upload filesystem first:
pio run --target uploadfs

# Then upload firmware:
pio run --target upload
```

### Step 2: Run Desktop App

```bash
cd /home/chuck/Desktop/meshcore_web_browser

# Get dependencies
flutter pub get

# Run on Linux
flutter run -d linux
```

### Step 3: Connect and Test

1. Power on your ESP32 with the firmware
2. Connect to WiFi network "MeshCore-Web" (password: meshcore123)
3. Open the Flutter app
4. The app will auto-connect to 192.168.4.1
5. Post a message to test!

## 🏗️ Architecture

```
┌─────────────────┐
│  Desktop App    │ ← User interacts here
│  (Flutter)      │
└────────┬────────┘
         │ WiFi
         │ WebSocket
┌────────▼────────┐
│  ESP32 Node     │ ← WiFi AP + Web Server
│  (Firmware)     │
└────────┬────────┘
         │ LoRa RF
         │ MeshCore Protocol
┌────────▼────────┐
│  Other Nodes    │ ← Messages relay across mesh
│  (Mesh Network) │
└─────────────────┘
```

## 📡 How It Works

### Local Connection
1. User connects to ESP32's WiFi AP
2. Desktop app connects via WebSocket to ESP32
3. User posts a message through the app
4. ESP32 broadcasts to all connected WiFi clients

### Mesh Network (TODO - needs implementation)
1. ESP32 converts message to MeshCore packet
2. Packet is transmitted over LoRa radio
3. Other nodes receive and forward the packet
4. Remote nodes display the message to their users
5. Messages propagate across the entire mesh

## 🔧 Configuration

### Firmware WiFi Credentials
Edit `meshcore-web-node/platformio.ini`:
```ini
-DWIFI_SSID=\"MeshCore-Web\"
-DWIFI_PASSWORD=\"meshcore123\"
```

### LoRa Frequency
Edit `meshcore-web-node/platformio.ini`:
```ini
-DLORA_FREQ=869.525  # Change to your region's frequency
```

### Desktop App Connection
In the Flutter app, click settings (⚙️) to change node IP address.

## 📋 TODO: Complete MeshCore Integration

The firmware currently has WiFi and web server working, but needs full MeshCore integration:

### In `meshcore-web-node/src/main.cpp`:

1. **Add MeshCore includes:**
```cpp
#include <Mesh.h>
#include <Packet.h>
#include <Identity.h>
```

2. **Initialize radio and mesh:**
```cpp
// Setup LoRa radio
radio_init();

// Create mesh instance
StdRNG fast_rng;
SimpleMeshTables tables;
MyMesh the_mesh(...);

// Load identity
IdentityStore store(SPIFFS, "/identity");
if (!store.load("_main", the_mesh.self_id)) {
  the_mesh.self_id = radio_new_identity();
  store.save("_main", the_mesh.self_id);
}
```

3. **Bridge web messages to mesh:**
```cpp
// When web message received:
void onWebSocketMessage(String content, String author) {
  // Create MeshCore packet
  // Send over LoRa
  the_mesh.sendPacket(...);
}
```

4. **Bridge mesh packets to web:**
```cpp
// In mesh receive callback:
void onMeshPacketReceived(Packet* pkt) {
  // Parse message
  // Broadcast to all WebSocket clients
  ws.textAll(json);
}
```

## 🎯 Current Status

### ✅ Working
- WiFi Access Point
- Web server with HTML/CSS/JS interface
- WebSocket communication
- Flutter desktop app
- Real-time message updates
- Beautiful UI

### ⚠️ Needs Implementation
- MeshCore library integration
- LoRa radio initialization
- Mesh packet creation/parsing
- Message relay between nodes
- Message persistence (SPIFFS storage)
- Node discovery

## 🔮 Future Enhancements

- **Mobile Apps**: Port Flutter app to Android/iOS
- **File Sharing**: Send small files over the mesh
- **Encryption**: End-to-end encrypted messages
- **Pages/Wiki**: Host static HTML pages on nodes
- **Node Discovery**: Auto-discover nearby nodes
- **Mesh Topology**: Visualize network structure
- **Offline Messages**: Queue messages when not connected

## 📂 Directory Structure

```
/home/chuck/Desktop/
├── T-Beam1watt/              # Original MeshCore firmware (reference)
├── meshcore-web-node/        # New web node firmware
│   ├── platformio.ini
│   ├── src/
│   │   └── main.cpp          # Main firmware (WiFi + Web server)
│   ├── data/                 # Web files (SPIFFS)
│   │   ├── index.html
│   │   ├── style.css
│   │   └── app.js
│   └── lib/
│       └── MeshCore/         # Symlink to T-Beam1watt/src
│
└── meshcore_web_browser/     # Flutter desktop app
    ├── lib/
    │   ├── main.dart
    │   ├── models/
    │   ├── services/
    │   └── screens/
    └── pubspec.yaml
```

## 🤝 Contributing

This is a starter project! The main work needed is:

1. Complete MeshCore integration in firmware
2. Implement mesh message relay
3. Add message persistence
4. Test with multiple nodes

## 📚 Resources

- [MeshCore GitHub](https://github.com/ripplebiz/MeshCore)
- [MeshCore Flasher](https://flasher.meshcore.co.uk)
- [Flutter Documentation](https://docs.flutter.dev/)
- [PlatformIO Docs](https://docs.platformio.org/)

## 📝 License

MIT License

---

**Created:** 2026-02-01  
**Purpose:** Text-based internet over LoRa mesh networks  
**Platform:** ESP32 + Flutter Desktop  
