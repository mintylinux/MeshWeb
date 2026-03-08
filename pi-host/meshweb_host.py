#!/usr/bin/env python3
"""
MeshWeb Pi Host Daemon
Connects to a MeshWeb companion device via USB serial or BLE,
and serves the web UI locally for browsing the LoRa mesh network.

Usage:
  python meshweb_host.py --serial /dev/ttyUSB0
  python meshweb_host.py --ble              # Auto-scan for MeshWeb BLE device
  python meshweb_host.py --ble MeshWeb-Swift-Fox  # Connect to specific device
"""

import argparse
import asyncio
import base64
import json
import logging
import sys
import threading
import time
from pathlib import Path

from flask import Flask, render_template, request, Response
from flask_sock import Sock

logging.basicConfig(level=logging.INFO, format='%(asctime)s [%(levelname)s] %(message)s')
log = logging.getLogger('meshweb')

# ============================================================
# Shared State
# ============================================================

class MeshWebState:
    """Shared state between serial/BLE reader and web server."""
    def __init__(self):
        self.ws_clients = []  # Active WebSocket connections
        self.current_page = ""  # Last downloaded page HTML
        self.page_lines = []  # Page content accumulator
        self.receiving_page = False
        self.lock = threading.Lock()

    def broadcast_event(self, json_str):
        """Send a JSON event to all connected WebSocket clients."""
        dead = []
        for ws in self.ws_clients:
            try:
                ws.send(json_str)
            except Exception:
                dead.append(ws)
        for ws in dead:
            self.ws_clients.remove(ws)

    def handle_line(self, line):
        """Process a line from the companion device."""
        line = line.strip()
        if not line:
            return

        # Structured event
        if line.startswith("EVT:"):
            json_str = line[4:]
            try:
                data = json.loads(json_str)
                log.info(f"Event: {data.get('type', '?')}")
                self.broadcast_event(json_str)
            except json.JSONDecodeError:
                log.warning(f"Bad JSON in EVT: {json_str[:80]}")
            return

        # Page content framing
        if line == "PAGE_START:":
            with self.lock:
                self.page_lines = []
                self.receiving_page = True
            log.info("Receiving page content...")
            return

        if line.startswith("PAGE_LINE:"):
            b64data = line[10:]
            with self.lock:
                if self.receiving_page:
                    self.page_lines.append(b64data)
            return

        if line == "PAGE_END:":
            with self.lock:
                self.receiving_page = False
                # Decode all base64 chunks
                raw = b""
                for chunk in self.page_lines:
                    try:
                        raw += base64.b64decode(chunk)
                    except Exception:
                        pass
                self.current_page = raw.decode('utf-8', errors='replace')
                self.page_lines = []
            log.info(f"Page received: {len(self.current_page)} bytes")
            return

        # Debug output (not structured)
        log.debug(f"Device: {line}")


state = MeshWebState()

# ============================================================
# USB Serial Connection
# ============================================================

class SerialConnection:
    def __init__(self, port, baud=115200):
        import serial
        self.ser = serial.Serial(port, baud, timeout=0.1)
        self.ser.dtr = True
        self.ser.rts = True
        time.sleep(0.5)  # Let device reset
        self.running = True
        log.info(f"USB Serial connected: {port} @ {baud}")

    def send_command(self, cmd):
        """Send a text command to the companion."""
        self.ser.write((cmd.strip() + '\n').encode('utf-8'))
        self.ser.flush()

    def read_loop(self):
        """Blocking read loop - run in a thread."""
        while self.running:
            try:
                line = self.ser.readline().decode('utf-8', errors='replace')
                if line:
                    state.handle_line(line)
            except Exception as e:
                log.error(f"Serial read error: {e}")
                time.sleep(0.5)

    def close(self):
        self.running = False
        self.ser.close()


# ============================================================
# BLE Connection (using bleak)
# ============================================================

NUS_SERVICE_UUID = "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
NUS_RX_UUID = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"  # We write here
NUS_TX_UUID = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"  # We get notifications here


class BLEConnection:
    def __init__(self):
        self.client = None
        self.running = True
        self._line_buffer = ""

    async def scan_and_connect(self, device_name=None):
        """Scan for MeshWeb BLE devices and connect."""
        from bleak import BleakScanner, BleakClient

        log.info("Scanning for MeshWeb BLE devices...")
        devices = await BleakScanner.discover(timeout=10.0)

        target = None
        for d in devices:
            name = d.name or ""
            if device_name:
                if name == device_name:
                    target = d
                    break
            elif name.startswith("MeshWeb-"):
                target = d
                break

        if not target:
            log.error("No MeshWeb BLE device found")
            return False

        log.info(f"Connecting to {target.name} ({target.address})...")
        self.client = BleakClient(target.address)
        await self.client.connect()

        if not self.client.is_connected:
            log.error("BLE connection failed")
            return False

        log.info(f"BLE connected: {target.name}")

        # Request higher MTU
        try:
            await self.client.request_mtu(512)
        except Exception:
            pass  # Not all platforms support explicit MTU request

        # Subscribe to TX notifications
        await self.client.start_notify(NUS_TX_UUID, self._on_notify)
        return True

    def _on_notify(self, sender, data):
        """Handle incoming BLE notification (may be chunked)."""
        text = data.decode('utf-8', errors='replace')
        self._line_buffer += text

        # Process complete lines
        while '\n' in self._line_buffer:
            line, self._line_buffer = self._line_buffer.split('\n', 1)
            state.handle_line(line)

    async def send_command(self, cmd):
        """Send a text command via BLE RX characteristic."""
        if self.client and self.client.is_connected:
            data = (cmd.strip() + '\n').encode('utf-8')
            await self.client.write_gatt_char(NUS_RX_UUID, data)

    async def run_loop(self):
        """Keep BLE connection alive."""
        while self.running:
            if self.client and not self.client.is_connected:
                log.warning("BLE disconnected")
                break
            await asyncio.sleep(0.5)

    async def close(self):
        self.running = False
        if self.client and self.client.is_connected:
            await self.client.disconnect()


# ============================================================
# Flask Web Server
# ============================================================

app = Flask(__name__, template_folder=str(Path(__file__).parent / 'templates'))
sock = Sock(app)

# Will be set to SerialConnection or BLEConnection
connection = None
ble_loop = None  # asyncio event loop for BLE


@app.route('/')
def index():
    return render_template('index.html')


@app.route('/page')
def serve_page():
    if state.current_page:
        return Response(state.current_page, mimetype='text/html')
    return "No page loaded yet", 404


@app.route('/meshgo')
def meshgo():
    node_id = request.args.get('node')
    page = request.args.get('page')
    if node_id and page:
        _send_cmd(f"meshgo {node_id} {page}")
        # Return a loading page
        html = f"""<html><head><style>
body{{background:#111;color:#fff;font-family:Arial;text-align:center;padding:50px}}
h1{{color:#0077ff}}
.status{{color:#888;margin-top:10px}}
</style></head><body>
<h1>Loading from {node_id}</h1>
<p>Requesting: <b>{page}</b></p>
<p class='status' id='status'>Sending request over LoRa...</p>
<script>
var ws=new WebSocket('ws://'+location.host+'/ws');
ws.onmessage=function(e){{try{{var d=JSON.parse(e.data);
if(d.type=='progress'){{document.getElementById('status').innerText='Downloading: '+d.received+'/'+d.total}}
else if(d.type=='page_complete'){{document.getElementById('status').innerText='Redirecting...';setTimeout(function(){{location.href='/page'}},500)}}
}}catch(e){{}}}};
</script></body></html>"""
        return Response(html, mimetype='text/html')
    return "Missing node or page parameter", 400


@sock.route('/ws')
def websocket(ws):
    state.ws_clients.append(ws)
    log.info(f"WebSocket client connected ({len(state.ws_clients)} total)")

    # Send initial list/companions request
    _send_cmd("list")
    _send_cmd("companions")

    try:
        while True:
            data = ws.receive()
            if data is None:
                break
            # Forward commands to companion
            _send_cmd(data)
    except Exception:
        pass
    finally:
        if ws in state.ws_clients:
            state.ws_clients.remove(ws)
        log.info(f"WebSocket client disconnected ({len(state.ws_clients)} total)")


def _send_cmd(cmd):
    """Send command to companion via whatever connection is active."""
    global connection, ble_loop
    if connection is None:
        return

    if isinstance(connection, SerialConnection):
        connection.send_command(cmd)
    elif isinstance(connection, BLEConnection):
        if ble_loop:
            asyncio.run_coroutine_threadsafe(connection.send_command(cmd), ble_loop)


# ============================================================
# Main
# ============================================================

def run_serial(port, baud):
    """Run with USB serial connection."""
    global connection
    connection = SerialConnection(port, baud)

    # Start serial reader thread
    reader = threading.Thread(target=connection.read_loop, daemon=True)
    reader.start()

    # Request initial data
    time.sleep(1)
    connection.send_command("list")
    connection.send_command("companions")

    log.info("Starting web server on http://0.0.0.0:8080")
    app.run(host='0.0.0.0', port=8080, debug=False)


def run_ble(device_name=None):
    """Run with BLE connection."""
    global connection, ble_loop

    connection = BLEConnection()

    async def ble_main():
        global ble_loop
        ble_loop = asyncio.get_event_loop()

        ok = await connection.scan_and_connect(device_name)
        if not ok:
            sys.exit(1)

        # Request initial data
        await asyncio.sleep(1)
        await connection.send_command("list")
        await connection.send_command("companions")

        # Keep BLE alive
        await connection.run_loop()

    # Run BLE in a background thread
    def ble_thread():
        loop = asyncio.new_event_loop()
        asyncio.set_event_loop(loop)
        global ble_loop
        ble_loop = loop
        loop.run_until_complete(ble_main())

    bt = threading.Thread(target=ble_thread, daemon=True)
    bt.start()

    # Give BLE time to connect
    time.sleep(12)

    log.info("Starting web server on http://0.0.0.0:8080")
    app.run(host='0.0.0.0', port=8080, debug=False)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='MeshWeb Pi Host Daemon')
    parser.add_argument('--serial', '-s', metavar='PORT',
                        help='USB serial port (e.g. /dev/ttyUSB0)')
    parser.add_argument('--baud', '-b', type=int, default=115200,
                        help='Serial baud rate (default: 115200)')
    parser.add_argument('--ble', nargs='?', const='', default=None, metavar='DEVICE_NAME',
                        help='Connect via BLE (optionally specify device name)')
    parser.add_argument('--port', '-p', type=int, default=8080,
                        help='Web server port (default: 8080)')

    args = parser.parse_args()

    if args.serial:
        run_serial(args.serial, args.baud)
    elif args.ble is not None:
        device_name = args.ble if args.ble else None
        run_ble(device_name)
    else:
        print("Error: specify --serial PORT or --ble")
        print("Examples:")
        print("  python meshweb_host.py --serial /dev/ttyUSB0")
        print("  python meshweb_host.py --ble")
        print("  python meshweb_host.py --ble 'MeshWeb-Swift Fox'")
        sys.exit(1)
