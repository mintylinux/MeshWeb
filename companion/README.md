# MeshCore Companion Node - ESP32 Xiao

Companion node for MeshWeb network. Listens for PAGE_ANNOUNCE broadcasts and requests pages over LoRa.

## Hardware Requirements

- **ESP32 Xiao** (any variant: C3, S3, etc.)
- **LoRa Module** - one of:
  - **SX1262** (newer, recommended) - requires BUSY pin
  - **SX1278 / RFM95 / RFM96** (older) - no BUSY pin

## Wiring

### Default Pin Configuration (in platformio.ini)

```
LORA_SCK=8    // SPI Clock
LORA_MISO=9   // SPI MISO
LORA_MOSI=10  // SPI MOSI
LORA_CS=7     // Chip Select
LORA_RST=6    // Reset
LORA_DIO1=5   // Interrupt pin
LORA_BUSY=4   // Busy pin (SX1262 only)
```

**Adjust these in `platformio.ini` to match your actual wiring!**

### For SX1278/RFM95 Modules

If you're using an older SX1278/RFM95 module:
1. In `src/main.cpp`, comment out line 22 and uncomment line 24:
   ```cpp
   // SX1262 radio = new Module(LORA_CS, LORA_DIO1, LORA_RST, LORA_BUSY);
   SX1278 radio = new Module(LORA_CS, LORA_DIO1, LORA_RST);
   ```
2. Change TCXO voltage in `setupLoRa()` from `1.8` to `0`:
   ```cpp
   int state = radio.begin(868.0, 250.0, 11, 5, 0x12, 22, 8, 0);
   ```

## Building and Flashing

1. **Connect your ESP32 Xiao** via USB

2. **Build and upload:**
   ```bash
   cd /home/chuck/Desktop/meshcore-companion-nrf
   platformio run --target upload --environment xiao_esp32c3
   ```

3. **Monitor serial output:**
   ```bash
   platformio device monitor
   ```

## Usage

Once running, the companion will:
- Listen for PAGE_ANNOUNCE broadcasts from web nodes
- Display discovered nodes and their available pages
- Allow you to request pages via serial commands

### Commands

Type commands in the serial monitor:

- **`list`** - Show all discovered web nodes and their pages
- **`req <node_index> <page>`** - Request a page from a node
  - Example: `req 0 /index.html`
- **`help`** - Show command list

### Example Session

```
Node ID: A3F1B290

✓ LoRa init success!
✓ Frequency: 868.0 MHz (MeshWeb Network)
✓ Listening for PAGE_ANNOUNCE broadcasts...

╔════════════════════════════════════════════╗
║  📡 PAGE_ANNOUNCE Received                 ║
╠════════════════════════════════════════════╣
║  Node ID: 9D3B0542                         ║
║  RSSI: -45.2 dBm  SNR: 8.5 dB              ║
║  Pages: 2                                  ║
║    1. /index.html                          ║
║    2. /style.css                           ║
╚════════════════════════════════════════════╝

> list

╔════════════════════════════════════════════╗
║      Discovered Web Nodes                  ║
╠════════════════════════════════════════════╣
║ [0] Node: 9D3B0542                         ║
║     Pages: 2                               ║
║       - /index.html                        ║
║       - /style.css                         ║
║     Last seen: 5 seconds ago               ║
╚════════════════════════════════════════════╝

> req 0 /index.html

→ Requesting page: /index.html
  From node: 9D3B0542
  Request ID: 173
✓ Request sent!

╔════════════════════════════════════════════╗
║  📄 PAGE_DATA Received                     ║
╠════════════════════════════════════════════╣
║  Chunk: 1/10                               ║
║  Size: 180 bytes                           ║
║  RSSI: -46.1 dBm  SNR: 8.2 dB              ║
╠════════════════════════════════════════════╣
║  Content:                                  ║
║  <!DOCTYPE html><html><head><meta charset="UTF-8">...
╚════════════════════════════════════════════╝
```

## Network Configuration

- **Frequency:** 915.0 MHz (US ISM band: 902-928 MHz)
- **Bandwidth:** 250 kHz
- **Spreading Factor:** 11
- **Sync Word:** 0x12 (private network)

This operates on a separate frequency from other networks.

## Troubleshooting

### LoRa init failed

Check:
- Wiring matches pin definitions in platformio.ini
- Power (3.3V) to LoRa module
- Correct radio type (SX1262 vs SX1278) in main.cpp
- TCXO voltage (1.8V for SX1262, 0 for SX1278)

### No PAGE_ANNOUNCE received

- Make sure web node (Heltec V3) is powered and running
- Check both devices are on same frequency (868.0 MHz)
- Verify sync word matches (0x12)
- Try moving devices closer together

### Request timeout

- Check target node ID is correct
- Page path must match exactly (case-sensitive)
- Web node must have the file in SPIFFS
