# MeshCore Web Node

A MeshCore firmware that hosts a text-based internet over LoRa mesh network. This firmware combines MeshCore repeater functionality with a WiFi access point and web server, allowing users to post and view messages over the mesh network through a web browser.

## Features

- **LoRa Mesh Network**: Full MeshCore repeater functionality
- **WiFi Access Point**: Creates a WiFi AP for users to connect to
- **Web Interface**: Modern HTML/CSS/JS interface for posting and viewing messages
- **Real-time Updates**: WebSocket-based communication for instant message delivery
- **Decentralized**: Messages propagate across the mesh network to other nodes
- **Offline-First**: Works without internet connectivity

## Hardware Requirements

- ESP32-based LoRa device (T-Beam, Heltec, etc.)
- LoRa radio module (SX1276/SX1278)
- Antenna

## Software Requirements

- PlatformIO
- Visual Studio Code (recommended)

## Installation

1. Clone or copy this project
2. Open in VS Code with PlatformIO
3. Connect your ESP32 device
4. Upload filesystem (SPIFFS) first:
   ```bash
   pio run --target uploadfs
   ```
5. Build and upload firmware:
   ```bash
   pio run --target upload
   ```

## Usage

### First Boot

1. Power on the device
2. Look for WiFi network named "MeshCore-Web" (password: meshcore123)
3. Connect to the WiFi network
4. Open browser and navigate to: http://192.168.4.1
5. Start posting messages!

### Changing WiFi Credentials

Edit `platformio.ini` and change:
```ini
-DWIFI_SSID=\"MeshCore-Web\"
-DWIFI_PASSWORD=\"meshcore123\"
```

## Architecture

### Firmware (ESP32)
- `src/main.cpp` - Main firmware code
  - WiFi AP setup
  - Web server (AsyncWebServer)
  - WebSocket handler
  - MeshCore integration (TODO)

### Web Interface (SPIFFS)
- `data/index.html` - Main HTML page
- `data/style.css` - Styling
- `data/app.js` - Frontend logic and WebSocket client

## How It Works

1. **Local Users**: Connect to the WiFi AP and access the web interface
2. **Post Message**: User posts a message through the web UI
3. **Local Broadcast**: WebSocket broadcasts to all connected clients
4. **Mesh Relay**: Message is sent over LoRa to other MeshCore nodes
5. **Remote Nodes**: Other nodes receive and display the message to their users

## TODO: MeshCore Integration

The current firmware is a starter template. To complete it, you need to:

1. **Link MeshCore Library**: 
   - Copy or symlink the MeshCore source files
   - Add proper includes for Mesh.h, Packet.h, etc.

2. **Initialize LoRa Radio**:
   - Add radio initialization code
   - Configure LoRa parameters (frequency, bandwidth, spreading factor)

3. **Setup Mesh Network**:
   - Create mesh instance
   - Load/generate node identity
   - Setup repeater functionality

4. **Bridge Messages**:
   - Convert web messages to MeshCore packets
   - Handle incoming MeshCore packets and push to web clients
   - Implement message synchronization between nodes

5. **Storage**:
   - Store messages in SPIFFS for persistence
   - Implement message history sync

## Project Structure

```
meshcore-web-node/
├── platformio.ini       # PlatformIO configuration
├── src/
│   └── main.cpp         # Main firmware code
├── data/                # Web files (uploaded to SPIFFS)
│   ├── index.html
│   ├── style.css
│   └── app.js
├── lib/
│   └── MeshCore/       # Symlink to MeshCore library
└── README.md
```

## Development

### Testing Locally
1. Upload filesystem: `pio run --target uploadfs`
2. Upload firmware: `pio run --target upload`
3. Open serial monitor: `pio device monitor`
4. Connect to WiFi "MeshCore-Web"
5. Open http://192.168.4.1

### Web Development
Edit files in `data/` folder and re-upload filesystem to test changes.

## Related Projects

- [MeshCore](https://github.com/ripplebiz/MeshCore) - Core mesh networking library
- [MeshCore Wardrive](../meshcore_wardrive_dev/) - Android app for coverage mapping

## License

MIT License

## Contributing

This is a starter project. Contributions welcome, especially for:
- Full MeshCore integration
- Message persistence and sync
- Node discovery UI
- Mobile-responsive design improvements
- Security and encryption
