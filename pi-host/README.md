# MeshWeb Pi Host

Connects to a MeshWeb companion device via **USB serial** or **BLE** and serves the web UI locally. This lets you use any computer (Raspberry Pi, laptop, etc.) as the browsing interface for the LoRa mesh network.

## Setup

```bash
pip install -r requirements.txt
```

## Usage

### USB Serial
```bash
python meshweb_host.py --serial /dev/ttyUSB0
```

### BLE (auto-scan)
```bash
python meshweb_host.py --ble
```

### BLE (specific device)
```bash
python meshweb_host.py --ble "MeshWeb-Swift Fox"
```

Then open `http://localhost:8080` in your browser.

## Options

| Flag | Description |
|------|-------------|
| `--serial PORT` | USB serial port (e.g. `/dev/ttyUSB0`, `/dev/ttyACM0`) |
| `--baud RATE` | Serial baud rate (default: 115200) |
| `--ble [NAME]` | Connect via BLE. Optionally specify device name. |
| `--port PORT` | Web server port (default: 8080) |

## Companion Firmware

Flash your LoRa device with the appropriate companion firmware:

| Build | Description |
|-------|-------------|
| `xiao_esp32s3` | WiFi + BLE + Serial (full featured) |
| `xiao_esp32s3_usb` | USB Serial only (smallest footprint) |
| `xiao_esp32s3_ble` | BLE + Serial (no WiFi) |
| `heltec_v3` | WiFi + BLE + Serial |
| `heltec_v3_usb` | USB Serial only |
| `heltec_v3_ble` | BLE + Serial |

## Architecture

```
┌─────────────┐  USB/BLE   ┌──────────────┐   LoRa    ┌──────────────┐
│  Pi / Laptop │◄──────────►│  Companion   │◄─────────►│  Host Node   │
│  (this daemon)│           │  (ESP32+LoRa) │           │  (serves HTML)│
│              │            │              │           │              │
│  Web UI on   │            │  Radio modem │           │  SPIFFS pages │
│  port 8080   │            │              │           │              │
└─────────────┘            └──────────────┘           └──────────────┘
       ▲
       │ WiFi/Ethernet
       ▼
  ┌──────────┐
  │  Browser  │
  └──────────┘
```
