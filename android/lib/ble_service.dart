import 'dart:async';
import 'dart:convert';
import 'dart:typed_data';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';

/// Nordic UART Service UUIDs (same as companion firmware)
const String NUS_SERVICE_UUID = "6e400001-b5a3-f393-e0a9-e50e24dcca9e";
const String NUS_RX_UUID = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"; // Write to this
const String NUS_TX_UUID = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"; // Notifications from this

/// Represents a discovered MeshWeb node (LoRa web host)
class MeshWebNode {
  final String name;
  final String id; // hex node ID
  final List<String> pages;
  DateTime lastSeen;

  MeshWebNode({
    required this.name,
    required this.id,
    required this.pages,
    DateTime? lastSeen,
  }) : lastSeen = lastSeen ?? DateTime.now();
}

/// Represents a discovered MeshWeb companion
class MeshWebCompanion {
  final String name;
  final String id;
  final int status;

  MeshWebCompanion({required this.name, required this.id, required this.status});
}

/// Represents a chat message (broadcast or DM)
class ChatMessage {
  final String fromId;
  final String fromName;
  final String message;
  final bool isSelf;
  final bool isBroadcast;
  final String? toId;
  final DateTime timestamp;

  ChatMessage({
    required this.fromId,
    required this.fromName,
    required this.message,
    required this.isSelf,
    required this.isBroadcast,
    this.toId,
    DateTime? timestamp,
  }) : timestamp = timestamp ?? DateTime.now();
}

/// Connection state for the BLE service
enum BleConnectionState { disconnected, scanning, connecting, connected }

/// BLE service for communicating with MeshWeb companion devices
class BleService {
  // Singleton
  static final BleService _instance = BleService._internal();
  factory BleService() => _instance;
  BleService._internal();

  // BLE state
  BluetoothDevice? _device;
  BluetoothCharacteristic? _rxCharacteristic; // We write commands here
  BluetoothCharacteristic? _txCharacteristic; // We get notifications here
  StreamSubscription? _notifySubscription;
  StreamSubscription? _connectionSubscription;

  // Data state
  final List<MeshWebNode> _nodes = [];
  final List<MeshWebCompanion> _companions = [];
  final List<String> _log = [];
  String _currentPage = '';
  bool _receivingPage = false;
  final StringBuffer _pageBuffer = StringBuffer();
  String _lineBuffer = '';
  int? _loadingNodeIndex;
  String _loadingStatus = '';
  int _loadingReceived = 0;
  int _loadingTotal = 0;

  // Chat state
  final List<ChatMessage> _broadcastMessages = [];
  final Map<String, List<ChatMessage>> _dmMessages = {};
  final Map<String, int> _unreadDm = {};
  int _unreadBroadcast = 0;

  // Stream controllers
  final _connectionStateController =
      StreamController<BleConnectionState>.broadcast();
  final _nodesController = StreamController<List<MeshWebNode>>.broadcast();
  final _companionsController =
      StreamController<List<MeshWebCompanion>>.broadcast();
  final _pageController = StreamController<String>.broadcast();
  final _logController = StreamController<String>.broadcast();
  final _progressController =
      StreamController<Map<String, dynamic>>.broadcast();
  final _scanResultsController =
      StreamController<List<ScanResult>>.broadcast();
  final _chatController = StreamController<ChatMessage>.broadcast();

  // Public getters
  BleConnectionState _state = BleConnectionState.disconnected;
  BleConnectionState get connectionState => _state;
  Stream<BleConnectionState> get connectionStateStream =>
      _connectionStateController.stream;
  Stream<List<MeshWebNode>> get nodesStream => _nodesController.stream;
  Stream<List<MeshWebCompanion>> get companionsStream =>
      _companionsController.stream;
  Stream<String> get pageStream => _pageController.stream;
  Stream<String> get logStream => _logController.stream;
  Stream<Map<String, dynamic>> get progressStream =>
      _progressController.stream;
  Stream<List<ScanResult>> get scanResultsStream =>
      _scanResultsController.stream;
  List<MeshWebNode> get nodes => List.unmodifiable(_nodes);
  List<MeshWebCompanion> get companions => List.unmodifiable(_companions);
  List<String> get log => List.unmodifiable(_log);
  String get currentPage => _currentPage;
  String? get deviceName => _device?.platformName;
  bool get isConnected => _state == BleConnectionState.connected;
  int? get loadingNodeIndex => _loadingNodeIndex;
  Stream<ChatMessage> get chatStream => _chatController.stream;
  List<ChatMessage> get broadcastMessages =>
      List.unmodifiable(_broadcastMessages);
  List<ChatMessage> dmMessages(String companionId) =>
      List.unmodifiable(_dmMessages[companionId] ?? []);
  int get unreadBroadcast => _unreadBroadcast;
  int unreadDmCount(String companionId) => _unreadDm[companionId] ?? 0;
  int get totalUnreadDm => _unreadDm.values.fold(0, (a, b) => a + b);

  void _setState(BleConnectionState state) {
    _state = state;
    _connectionStateController.add(state);
  }

  void _addLog(String message) {
    _log.add(message);
    if (_log.length > 500) _log.removeAt(0);
    _logController.add(message);
  }

  // ============================================================
  // SCANNING
  // ============================================================

  /// Scan for MeshWeb BLE devices
  Future<void> startScan({Duration timeout = const Duration(seconds: 10)}) async {
    if (_state == BleConnectionState.scanning) return;

    try {
      if (await FlutterBluePlus.isSupported == false) {
        _addLog('ERROR: Bluetooth not supported');
        return;
      }

      // Turn on Bluetooth if needed
      if (await FlutterBluePlus.adapterState.first !=
          BluetoothAdapterState.on) {
        _addLog('Waiting for Bluetooth adapter...');
        await FlutterBluePlus.adapterState
            .firstWhere((s) => s == BluetoothAdapterState.on)
            .timeout(const Duration(seconds: 5));
      }

      _setState(BleConnectionState.scanning);
      _addLog('Scanning for MeshWeb devices...');

      // Listen to scan results and forward filtered results
      final subscription = FlutterBluePlus.scanResults.listen((results) {
        final meshwebResults = results.where((r) {
          final name = r.device.platformName.toLowerCase();
          return name.contains('meshweb');
        }).toList();
        _scanResultsController.add(meshwebResults);
      });

      await FlutterBluePlus.startScan(
        timeout: timeout,
        androidScanMode: AndroidScanMode.lowLatency,
      );

      await Future.delayed(timeout);
      await subscription.cancel();

      if (_state == BleConnectionState.scanning) {
        _setState(BleConnectionState.disconnected);
      }
      _addLog('Scan complete');
    } catch (e) {
      _addLog('Scan error: $e');
      _setState(BleConnectionState.disconnected);
    }
  }

  Future<void> stopScan() async {
    await FlutterBluePlus.stopScan();
    if (_state == BleConnectionState.scanning) {
      _setState(BleConnectionState.disconnected);
    }
  }

  // ============================================================
  // CONNECTION
  // ============================================================

  /// Connect to a MeshWeb companion device
  Future<bool> connect(BluetoothDevice device) async {
    try {
      _setState(BleConnectionState.connecting);
      _addLog('Connecting to ${device.platformName}...');

      await device.connect(timeout: const Duration(seconds: 15));
      _device = device;

      // Request higher MTU for larger JSON events
      await device.requestMtu(512);

      // Discover services
      List<BluetoothService> services = await device.discoverServices();

      // Find Nordic UART Service
      for (BluetoothService service in services) {
        if (service.uuid.toString().toLowerCase().contains('6e40')) {
          for (BluetoothCharacteristic char in service.characteristics) {
            final uuid = char.uuid.toString().toLowerCase();
            if (uuid.contains('6e400002')) {
              _rxCharacteristic = char; // We write to RX
            }
            if (uuid.contains('6e400003')) {
              _txCharacteristic = char; // We read from TX
              await char.setNotifyValue(true);
              _notifySubscription =
                  char.lastValueStream.listen(_handleNotification);
            }
          }
        }
      }

      if (_rxCharacteristic == null || _txCharacteristic == null) {
        _addLog('ERROR: NUS service not found on device');
        await device.disconnect();
        _setState(BleConnectionState.disconnected);
        return false;
      }

      // Monitor connection state
      _connectionSubscription = device.connectionState.listen((state) {
        if (state == BluetoothConnectionState.disconnected) {
          _handleDisconnect();
        }
      });

      _setState(BleConnectionState.connected);
      _addLog('Connected to ${device.platformName}');

      // Request initial data
      await Future.delayed(const Duration(milliseconds: 500));
      await sendCommand('list');
      await Future.delayed(const Duration(milliseconds: 200));
      await sendCommand('companions');

      return true;
    } catch (e) {
      _addLog('Connection error: $e');
      _setState(BleConnectionState.disconnected);
      return false;
    }
  }

  void _handleDisconnect() {
    _addLog('Device disconnected');
    _notifySubscription?.cancel();
    _connectionSubscription?.cancel();
    _device = null;
    _rxCharacteristic = null;
    _txCharacteristic = null;
    _setState(BleConnectionState.disconnected);
  }

  Future<void> disconnect() async {
    try {
      await _notifySubscription?.cancel();
      await _connectionSubscription?.cancel();
      await _device?.disconnect();
    } catch (_) {}
    _device = null;
    _rxCharacteristic = null;
    _txCharacteristic = null;
    _setState(BleConnectionState.disconnected);
    _addLog('Disconnected');
  }

  // ============================================================
  // SEND COMMANDS
  // ============================================================

  /// Send a text command to the companion
  Future<void> sendCommand(String command) async {
    if (_rxCharacteristic == null) return;
    try {
      _addLog('> $command');
      final data = utf8.encode('$command\n');
      await _rxCharacteristic!.write(data, withoutResponse: false);
    } catch (e) {
      _addLog('Send error: $e');
    }
  }

  /// Request a page from a node by index
  /// The companion will send a page_loaded EVT when ready, which triggers getpage automatically
  Future<void> requestPage(int nodeIndex, String page) async {
    _loadingNodeIndex = nodeIndex;
    _loadingStatus = 'Requesting...';
    _loadingReceived = 0;
    _loadingTotal = 0;
    _progressController.add({
      'nodeIndex': nodeIndex,
      'status': 'requesting',
    });
    await sendCommand('req $nodeIndex $page');
  }

  /// Cancel an in-progress page request
  void cancelPageRequest() {
    _loadingNodeIndex = null;
    _loadingStatus = '';
    _loadingReceived = 0;
    _loadingTotal = 0;
    _receivingPage = false;
    _pageBuffer.clear();
    _progressController.add({'status': 'done'});
    _addLog('Page request cancelled');
  }

  /// Request a page by node hex ID
  Future<void> requestPageById(String nodeId, String page) async {
    await sendCommand('meshgo $nodeId $page');
  }

  /// Send a broadcast message to all companions
  Future<void> sendBroadcast(String text) async {
    await sendCommand('msg $text');
  }

  /// Send a direct message to a specific companion
  Future<void> sendDirectMessage(String companionId, String text) async {
    final index = _companions.indexWhere((c) => c.id == companionId);
    if (index >= 0) {
      await sendCommand('dm $index $text');
    } else {
      _addLog('ERROR: Companion $companionId not found');
    }
  }

  /// Mark broadcast messages as read
  void markBroadcastRead() {
    _unreadBroadcast = 0;
  }

  /// Mark DM messages from a companion as read
  void markDmRead(String companionId) {
    _unreadDm[companionId] = 0;
  }

  // ============================================================
  // RECEIVE & PARSE
  // ============================================================

  void _handleNotification(List<int> value) {
    if (value.isEmpty) return;

    final chunk = utf8.decode(Uint8List.fromList(value), allowMalformed: true);
    _lineBuffer += chunk;

    // Process complete lines
    while (_lineBuffer.contains('\n')) {
      final idx = _lineBuffer.indexOf('\n');
      final line = _lineBuffer.substring(0, idx).trim();
      _lineBuffer = _lineBuffer.substring(idx + 1);
      if (line.isNotEmpty) {
        _processLine(line);
      }
    }
  }

  void _processLine(String line) {
    _addLog(line);

    // Page content transfer
    if (line == 'PAGE_START:') {
      _receivingPage = true;
      _pageBuffer.clear();
      return;
    }
    if (line == 'PAGE_END:') {
      _receivingPage = false;
      _currentPage = _pageBuffer.toString();
      _loadingNodeIndex = null;
      _loadingStatus = '';
      _progressController.add({'status': 'done'});
      _pageController.add(_currentPage);
      _addLog('Page received: ${_currentPage.length} bytes');
      return;
    }
    if (_receivingPage && line.startsWith('PAGE_LINE:')) {
      final b64 = line.substring(10);
      try {
        final decoded = utf8.decode(base64Decode(b64));
        _pageBuffer.write(decoded);
      } catch (_) {
        _addLog('Base64 decode error');
      }
      return;
    }

    // Fallback: "Sent N bytes as base64" comes right after PAGE_END
    // If we see it while still receiving, PAGE_END notification was lost
    if (_receivingPage && line.startsWith('Sent ') && line.contains('base64')) {
      _receivingPage = false;
      _currentPage = _pageBuffer.toString();
      _loadingNodeIndex = null;
      _loadingStatus = '';
      _progressController.add({'status': 'done'});
      _pageController.add(_currentPage);
      _addLog('Page received (fallback): ${_currentPage.length} bytes');
      return;
    }

    // Parse EVT: JSON events
    if (line.startsWith('EVT:')) {
      final jsonStr = line.substring(4);
      try {
        final data = jsonDecode(jsonStr) as Map<String, dynamic>;
        _handleEvent(data);
      } catch (e) {
        _addLog('JSON parse error: $e');
      }
      return;
    }
  }

  void _handleEvent(Map<String, dynamic> data) {
    final type = data['type'] as String?;
    if (type == null) return;

    switch (type) {
      case 'nodes':
        _nodes.clear();
        final nodeList = data['nodes'] as List<dynamic>? ?? [];
        for (final n in nodeList) {
          final pages = (n['pages'] as List<dynamic>?)
                  ?.map((p) => p.toString())
                  .toList() ??
              ['/'];
          _nodes.add(MeshWebNode(
            name: n['name'] ?? 'Unknown',
            id: n['id'] ?? '',
            pages: pages,
          ));
        }
        _nodesController.add(List.from(_nodes));
        break;

      case 'companions':
        _companions.clear();
        final compList = data['companions'] as List<dynamic>? ?? [];
        for (final c in compList) {
          _companions.add(MeshWebCompanion(
            name: c['name'] ?? 'Unknown',
            id: c['id'] ?? '',
            status: c['status'] ?? 0,
          ));
        }
        _companionsController.add(List.from(_companions));
        break;

      case 'page_start':
        _loadingStatus = 'Receiving...';
        _progressController.add({
          'nodeIndex': _loadingNodeIndex,
          'status': 'receiving',
        });
        _addLog('Page transfer started...');
        break;

      case 'progress':
        final received = data['received'] ?? 0;
        final total = data['total'] ?? 0;
        _loadingReceived = received;
        _loadingTotal = total;
        _loadingStatus = 'Chunk $received/$total';
        _progressController.add({
          'nodeIndex': _loadingNodeIndex,
          'status': 'progress',
          'received': received,
          'total': total,
        });
        _addLog('Page progress: $received/$total chunks');
        break;

      case 'page_complete':
        final size = data['size'] ?? 0;
        _loadingStatus = 'Loading page...';
        _progressController.add({
          'nodeIndex': _loadingNodeIndex,
          'status': 'loading',
        });
        _addLog('Page complete ($size bytes), loading...');
        // Companion auto-sends page data over BLE; fallback to getpage after timeout
        final loadingIdx = _loadingNodeIndex;
        Future.delayed(const Duration(seconds: 5), () {
          if (_loadingNodeIndex == loadingIdx && loadingIdx != null) {
            _addLog('Auto-send timeout, trying getpage...');
            sendCommand('getpage');
          }
        });
        break;

      case 'node_discovered':
        sendCommand('list');
        break;

      case 'chat':
        final chatMsg = ChatMessage(
          fromId: data['from_id'] ?? '',
          fromName: data['from'] ?? 'Unknown',
          message: data['msg'] ?? '',
          isSelf: data['self'] == true,
          isBroadcast: data['broadcast'] == true,
          toId: data['to_id'] as String?,
        );
        if (chatMsg.isBroadcast) {
          _broadcastMessages.add(chatMsg);
          if (_broadcastMessages.length > 200) _broadcastMessages.removeAt(0);
          if (!chatMsg.isSelf) _unreadBroadcast++;
        } else {
          final peerId =
              chatMsg.isSelf ? (chatMsg.toId ?? '') : chatMsg.fromId;
          _dmMessages.putIfAbsent(peerId, () => []);
          _dmMessages[peerId]!.add(chatMsg);
          if (_dmMessages[peerId]!.length > 200) {
            _dmMessages[peerId]!.removeAt(0);
          }
          if (!chatMsg.isSelf) {
            _unreadDm[peerId] = (_unreadDm[peerId] ?? 0) + 1;
          }
        }
        _chatController.add(chatMsg);
        _addLog(
            '${chatMsg.isBroadcast ? "📢" : "💬"} ${chatMsg.fromName}: ${chatMsg.message}');
        break;
    }
  }

  void dispose() {
    disconnect();
    _connectionStateController.close();
    _nodesController.close();
    _companionsController.close();
    _pageController.close();
    _logController.close();
    _progressController.close();
    _scanResultsController.close();
    _chatController.close();
  }
}
