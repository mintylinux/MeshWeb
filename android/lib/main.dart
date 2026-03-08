import 'dart:async';
import 'package:flutter/material.dart';
import 'package:webview_flutter/webview_flutter.dart';
import 'package:connectivity_plus/connectivity_plus.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import 'ble_service.dart';

void main() {
  runApp(const MeshWebApp());
}

class MeshWebApp extends StatelessWidget {
  const MeshWebApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'MeshWeb',
      theme: ThemeData(
        colorScheme: ColorScheme.fromSeed(
          seedColor: const Color(0xFF6C3FA0),
          brightness: Brightness.light,
        ),
        useMaterial3: true,
      ),
      darkTheme: ThemeData(
        colorScheme: ColorScheme.fromSeed(
          seedColor: const Color(0xFF6C3FA0),
          brightness: Brightness.dark,
        ),
        useMaterial3: true,
      ),
      themeMode: ThemeMode.system,
      home: const ConnectionScreen(),
      debugShowCheckedModeBanner: false,
    );
  }
}

// ============================================================
// CONNECTION SCREEN - Choose WiFi or Bluetooth
// ============================================================

class ConnectionScreen extends StatelessWidget {
  const ConnectionScreen({super.key});

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return Scaffold(
      body: SafeArea(
        child: Padding(
          padding: const EdgeInsets.all(24.0),
          child: Column(
            children: [
              const SizedBox(height: 40),
              Icon(Icons.language, size: 64, color: theme.colorScheme.primary),
              const SizedBox(height: 16),
              Text('MeshWeb',
                  style: theme.textTheme.headlineLarge
                      ?.copyWith(fontWeight: FontWeight.bold)),
              const SizedBox(height: 8),
              Text('Decentralized Web over LoRa',
                  style: theme.textTheme.bodyLarge
                      ?.copyWith(color: theme.colorScheme.onSurfaceVariant)),
              const SizedBox(height: 48),
              _ConnectionCard(
                icon: Icons.wifi,
                title: 'Connect via WiFi',
                subtitle: 'Join companion WiFi AP and browse via WebView',
                onTap: () => Navigator.push(
                  context,
                  MaterialPageRoute(builder: (_) => const WiFiBrowserScreen()),
                ),
              ),
              const SizedBox(height: 16),
              _ConnectionCard(
                icon: Icons.bluetooth,
                title: 'Connect via Bluetooth',
                subtitle: 'Scan for MeshWeb companions over BLE',
                onTap: () => Navigator.push(
                  context,
                  MaterialPageRoute(builder: (_) => const BLEScanScreen()),
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }
}

class _ConnectionCard extends StatelessWidget {
  final IconData icon;
  final String title;
  final String subtitle;
  final VoidCallback onTap;

  const _ConnectionCard({
    required this.icon,
    required this.title,
    required this.subtitle,
    required this.onTap,
  });

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return Card(
      elevation: 2,
      child: InkWell(
        onTap: onTap,
        borderRadius: BorderRadius.circular(12),
        child: Padding(
          padding: const EdgeInsets.all(20.0),
          child: Row(
            children: [
              Container(
                padding: const EdgeInsets.all(12),
                decoration: BoxDecoration(
                  color: theme.colorScheme.primaryContainer,
                  borderRadius: BorderRadius.circular(12),
                ),
                child: Icon(icon, size: 32,
                    color: theme.colorScheme.onPrimaryContainer),
              ),
              const SizedBox(width: 16),
              Expanded(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text(title, style: theme.textTheme.titleMedium
                        ?.copyWith(fontWeight: FontWeight.w600)),
                    const SizedBox(height: 4),
                    Text(subtitle, style: theme.textTheme.bodySmall
                        ?.copyWith(
                            color: theme.colorScheme.onSurfaceVariant)),
                  ],
                ),
              ),
              Icon(Icons.chevron_right,
                  color: theme.colorScheme.onSurfaceVariant),
            ],
          ),
        ),
      ),
    );
  }
}

// ============================================================
// WIFI BROWSER SCREEN - Existing WebView functionality
// ============================================================

class WiFiBrowserScreen extends StatefulWidget {
  const WiFiBrowserScreen({super.key});

  @override
  State<WiFiBrowserScreen> createState() => _WiFiBrowserScreenState();
}

class _WiFiBrowserScreenState extends State<WiFiBrowserScreen> {
  late final WebViewController _controller;
  String _currentUrl = 'http://192.168.4.1';
  bool _isLoading = true;
  bool _isConnected = false;
  final TextEditingController _urlController =
      TextEditingController(text: '192.168.4.1');

  @override
  void initState() {
    super.initState();
    _checkConnectivity();
    _initializeWebView();
  }

  void _initializeWebView() {
    _controller = WebViewController()
      ..setJavaScriptMode(JavaScriptMode.unrestricted)
      ..setNavigationDelegate(
        NavigationDelegate(
          onPageStarted: (_) => setState(() => _isLoading = true),
          onPageFinished: (url) =>
              setState(() { _isLoading = false; _currentUrl = url; }),
          onWebResourceError: (error) {
            setState(() => _isLoading = false);
            if (mounted) {
              ScaffoldMessenger.of(context).showSnackBar(
                SnackBar(content: Text('Error: ${error.description}'),
                    backgroundColor: Colors.red),
              );
            }
          },
        ),
      )
      ..loadRequest(Uri.parse(_currentUrl));
  }

  Future<void> _checkConnectivity() async {
    final connectivity = Connectivity();
    final result = await connectivity.checkConnectivity();
    setState(() {
      _isConnected = result.contains(ConnectivityResult.wifi);
    });
    connectivity.onConnectivityChanged.listen((results) {
      setState(() {
        _isConnected = results.contains(ConnectivityResult.wifi);
      });
    });
  }

  void _showUrlDialog() {
    showDialog(
      context: context,
      builder: (context) => AlertDialog(
        title: const Text('Connect to Node'),
        content: TextField(
          controller: _urlController,
          decoration: const InputDecoration(
            labelText: 'Node IP Address',
            hintText: '192.168.4.1',
            border: OutlineInputBorder(),
          ),
          keyboardType: TextInputType.url,
          autofocus: true,
          onSubmitted: (value) {
            Navigator.pop(context);
            _navigateToUrl(value);
          },
        ),
        actions: [
          TextButton(onPressed: () => Navigator.pop(context),
              child: const Text('Cancel')),
          FilledButton(
            onPressed: () {
              Navigator.pop(context);
              _navigateToUrl(_urlController.text);
            },
            child: const Text('Connect'),
          ),
        ],
      ),
    );
  }

  void _navigateToUrl(String address) {
    String url = address.trim();
    if (!url.startsWith('http://') && !url.startsWith('https://')) {
      url = 'http://$url';
    }
    setState(() => _currentUrl = url);
    _controller.loadRequest(Uri.parse(url));
  }

  @override
  void dispose() {
    _urlController.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('MeshWeb - WiFi'),
        actions: [
          Padding(
            padding: const EdgeInsets.symmetric(horizontal: 8.0),
            child: Row(children: [
              Icon(_isConnected ? Icons.wifi : Icons.wifi_off,
                  color: _isConnected ? Colors.green : Colors.red, size: 20),
              const SizedBox(width: 4),
              Text(_isConnected ? 'WiFi' : 'No WiFi',
                  style: Theme.of(context).textTheme.bodySmall),
            ]),
          ),
          IconButton(icon: const Icon(Icons.refresh),
              onPressed: () => _controller.reload(), tooltip: 'Reload'),
          IconButton(icon: const Icon(Icons.link),
              onPressed: _showUrlDialog, tooltip: 'Change Node'),
        ],
      ),
      body: Stack(children: [
        WebViewWidget(controller: _controller),
        if (_isLoading) const Center(child: CircularProgressIndicator()),
      ]),
      bottomNavigationBar: BottomAppBar(
        child: Row(
          mainAxisAlignment: MainAxisAlignment.spaceEvenly,
          children: [
            IconButton(icon: const Icon(Icons.arrow_back),
                onPressed: () => _controller.goBack(), tooltip: 'Back'),
            IconButton(icon: const Icon(Icons.arrow_forward),
                onPressed: () => _controller.goForward(), tooltip: 'Forward'),
            IconButton(icon: const Icon(Icons.home),
                onPressed: () => _navigateToUrl('192.168.4.1'),
                tooltip: 'Home'),
            Expanded(
              child: Padding(
                padding: const EdgeInsets.symmetric(horizontal: 16.0),
                child: Text(_currentUrl,
                    style: Theme.of(context).textTheme.bodySmall,
                    overflow: TextOverflow.ellipsis, textAlign: TextAlign.center),
              ),
            ),
          ],
        ),
      ),
    );
  }
}

// ============================================================
// BLE SCAN SCREEN - Find and connect to MeshWeb devices
// ============================================================

class BLEScanScreen extends StatefulWidget {
  const BLEScanScreen({super.key});

  @override
  State<BLEScanScreen> createState() => _BLEScanScreenState();
}

class _BLEScanScreenState extends State<BLEScanScreen> {
  final _ble = BleService();
  List<ScanResult> _scanResults = [];
  bool _isScanning = false;

  @override
  void initState() {
    super.initState();
    _ble.scanResultsStream.listen((results) {
      if (mounted) setState(() => _scanResults = results);
    });
    _ble.connectionStateStream.listen((state) {
      if (mounted) setState(() {
        _isScanning = state == BleConnectionState.scanning;
      });
      if (state == BleConnectionState.connected && mounted) {
        Navigator.pushReplacement(
          context,
          MaterialPageRoute(builder: (_) => const BLEBrowserScreen()),
        );
      }
    });
    // Start scanning immediately
    _ble.startScan();
  }

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return Scaffold(
      appBar: AppBar(
        title: const Text('Scan for Devices'),
        actions: [
          if (_isScanning)
            const Padding(
              padding: EdgeInsets.all(16.0),
              child: SizedBox(width: 20, height: 20,
                  child: CircularProgressIndicator(strokeWidth: 2)),
            )
          else
            IconButton(
              icon: const Icon(Icons.refresh),
              onPressed: () => _ble.startScan(),
              tooltip: 'Rescan',
            ),
        ],
      ),
      body: _scanResults.isEmpty
          ? Center(
              child: Column(
                mainAxisSize: MainAxisSize.min,
                children: [
                  Icon(Icons.bluetooth_searching, size: 64,
                      color: theme.colorScheme.onSurfaceVariant),
                  const SizedBox(height: 16),
                  Text(
                    _isScanning
                        ? 'Scanning for MeshWeb devices...'
                        : 'No MeshWeb devices found',
                    style: theme.textTheme.bodyLarge,
                  ),
                  if (!_isScanning) ...[
                    const SizedBox(height: 16),
                    FilledButton.icon(
                      onPressed: () => _ble.startScan(),
                      icon: const Icon(Icons.refresh),
                      label: const Text('Scan Again'),
                    ),
                  ],
                ],
              ),
            )
          : ListView.builder(
              itemCount: _scanResults.length,
              itemBuilder: (context, index) {
                final result = _scanResults[index];
                final name = result.device.platformName.isNotEmpty
                    ? result.device.platformName
                    : 'Unknown';
                final rssi = result.rssi;
                return ListTile(
                  leading: Container(
                    padding: const EdgeInsets.all(8),
                    decoration: BoxDecoration(
                      color: theme.colorScheme.primaryContainer,
                      borderRadius: BorderRadius.circular(8),
                    ),
                    child: Icon(Icons.bluetooth,
                        color: theme.colorScheme.onPrimaryContainer),
                  ),
                  title: Text(name),
                  subtitle: Text('RSSI: $rssi dBm'),
                  trailing: _ble.connectionState ==
                          BleConnectionState.connecting
                      ? const SizedBox(width: 20, height: 20,
                          child: CircularProgressIndicator(strokeWidth: 2))
                      : const Icon(Icons.chevron_right),
                  onTap: () async {
                    await _ble.stopScan();
                    await _ble.connect(result.device);
                  },
                );
              },
            ),
    );
  }
}

// ============================================================
// BLE BROWSER SCREEN - Native UI for BLE-connected browsing
// ============================================================

class BLEBrowserScreen extends StatefulWidget {
  const BLEBrowserScreen({super.key});

  @override
  State<BLEBrowserScreen> createState() => _BLEBrowserScreenState();
}

class _BLEBrowserScreenState extends State<BLEBrowserScreen> {
  final _ble = BleService();
  int _tabIndex = 0;
  List<MeshWebNode> _nodes = [];
  List<MeshWebCompanion> _companions = [];
  final List<String> _logLines = [];
  int? _loadingNodeIndex;
  String _loadingStatus = '';
  double _loadingProgress = 0.0;
  List<ChatMessage> _broadcastMessages = [];

  @override
  void initState() {
    super.initState();
    _ble.nodesStream.listen((nodes) {
      if (mounted) setState(() => _nodes = nodes);
    });
    _ble.companionsStream.listen((companions) {
      if (mounted) setState(() => _companions = companions);
    });
    _ble.logStream.listen((line) {
      if (mounted) {
        setState(() {
          _logLines.add(line);
          if (_logLines.length > 200) _logLines.removeAt(0);
        });
      }
    });
    _ble.pageStream.listen((html) {
      if (mounted) {
        _showPageViewer(html);
      }
    });
    _ble.progressStream.listen((progress) {
      if (mounted) {
        setState(() {
          final status = progress['status'];
          if (status == 'done') {
            _loadingNodeIndex = null;
            _loadingStatus = '';
            _loadingProgress = 0.0;
          } else {
            _loadingNodeIndex = progress['nodeIndex'] as int?;
            if (status == 'requesting') {
              _loadingStatus = 'Requesting page...';
              _loadingProgress = 0.0;
            } else if (status == 'receiving') {
              _loadingStatus = 'Receiving data...';
              _loadingProgress = 0.05;
            } else if (status == 'progress') {
              final received = progress['received'] as int? ?? 0;
              final total = progress['total'] as int? ?? 1;
              _loadingStatus = 'Downloading $received/$total';
              _loadingProgress = total > 0 ? received / total : 0.0;
            } else if (status == 'loading') {
              _loadingStatus = 'Loading page...';
              _loadingProgress = 1.0;
            }
          }
        });
      }
    });
    _ble.chatStream.listen((msg) {
      if (mounted) {
        setState(() {
          _broadcastMessages = _ble.broadcastMessages.toList();
        });
      }
    });
    _ble.connectionStateStream.listen((state) {
      if (state == BleConnectionState.disconnected && mounted) {
        Navigator.pop(context);
      }
    });
    // Load initial state
    _nodes = _ble.nodes.toList();
    _companions = _ble.companions.toList();
    _broadcastMessages = _ble.broadcastMessages.toList();
    _logLines.addAll(_ble.log);
  }

  void _showPageViewer(String html) {
    Navigator.push(
      context,
      MaterialPageRoute(
        builder: (_) => _PageViewerScreen(html: html),
      ),
    );
  }

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return Scaffold(
      appBar: AppBar(
        title: Text(_ble.deviceName ?? 'MeshWeb BLE'),
        leading: IconButton(
          icon: const Icon(Icons.bluetooth_connected),
          onPressed: null,
        ),
        actions: [
          IconButton(
            icon: const Icon(Icons.refresh),
            onPressed: () {
              _ble.sendCommand('list');
              _ble.sendCommand('companions');
            },
            tooltip: 'Refresh',
          ),
          IconButton(
            icon: const Icon(Icons.link_off),
            onPressed: () => _ble.disconnect(),
            tooltip: 'Disconnect',
          ),
        ],
      ),
      body: IndexedStack(
        index: _tabIndex,
        children: [
          _buildNodesTab(theme),
          _buildCompanionsTab(theme),
          _buildLogTab(theme),
        ],
      ),
      bottomNavigationBar: NavigationBar(
        selectedIndex: _tabIndex,
        onDestinationSelected: (i) {
          setState(() => _tabIndex = i);
          if (i == 1) _ble.markBroadcastRead();
        },
        destinations: [
          NavigationDestination(
            icon: Badge(
              label: Text('${_nodes.length}'),
              isLabelVisible: _nodes.isNotEmpty,
              child: const Icon(Icons.dns_outlined),
            ),
            selectedIcon: Badge(
              label: Text('${_nodes.length}'),
              isLabelVisible: _nodes.isNotEmpty,
              child: const Icon(Icons.dns),
            ),
            label: 'Nodes',
          ),
          NavigationDestination(
            icon: Badge(
              label: Text('${_ble.unreadBroadcast + _ble.totalUnreadDm}'),
              isLabelVisible:
                  _ble.unreadBroadcast + _ble.totalUnreadDm > 0,
              child: const Icon(Icons.chat_bubble_outline),
            ),
            selectedIcon: Badge(
              label: Text('${_ble.unreadBroadcast + _ble.totalUnreadDm}'),
              isLabelVisible:
                  _ble.unreadBroadcast + _ble.totalUnreadDm > 0,
              child: const Icon(Icons.chat_bubble),
            ),
            label: 'Chat',
          ),
          const NavigationDestination(
            icon: Icon(Icons.terminal_outlined),
            selectedIcon: Icon(Icons.terminal),
            label: 'Log',
          ),
        ],
      ),
    );
  }

  Widget _buildNodesTab(ThemeData theme) {
    if (_nodes.isEmpty) {
      return Center(
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            Icon(Icons.search, size: 64,
                color: theme.colorScheme.onSurfaceVariant),
            const SizedBox(height: 16),
            Text('Listening for web nodes...',
                style: theme.textTheme.bodyLarge),
            const SizedBox(height: 8),
            Text('Nodes will appear as they broadcast',
                style: theme.textTheme.bodySmall
                    ?.copyWith(color: theme.colorScheme.onSurfaceVariant)),
          ],
        ),
      );
    }
    return ListView.builder(
      itemCount: _nodes.length,
      itemBuilder: (context, index) {
        final node = _nodes[index];
        final isLoading = _loadingNodeIndex == index;
        return Card(
          margin: const EdgeInsets.symmetric(horizontal: 12, vertical: 4),
          child: Column(
            children: [
              ListTile(
                leading: Container(
                  padding: const EdgeInsets.all(8),
                  decoration: BoxDecoration(
                    color: theme.colorScheme.tertiaryContainer,
                    borderRadius: BorderRadius.circular(8),
                  ),
                  child: isLoading
                      ? SizedBox(
                          width: 24,
                          height: 24,
                          child: CircularProgressIndicator(
                            strokeWidth: 2.5,
                            color: theme.colorScheme.onTertiaryContainer,
                          ),
                        )
                      : Icon(Icons.language,
                          color: theme.colorScheme.onTertiaryContainer),
                ),
                title: Text(node.name),
                subtitle: Text(isLoading
                    ? _loadingStatus
                    : 'ID: ${node.id} · ${node.pages.length} page(s)'),
                trailing: isLoading
                    ? FilledButton.tonal(
                        onPressed: () => _ble.cancelPageRequest(),
                        style: FilledButton.styleFrom(
                          backgroundColor:
                              theme.colorScheme.errorContainer,
                          foregroundColor:
                              theme.colorScheme.onErrorContainer,
                        ),
                        child: const Row(
                          mainAxisSize: MainAxisSize.min,
                          children: [
                            Icon(Icons.close, size: 16),
                            SizedBox(width: 4),
                            Text('Stop'),
                          ],
                        ),
                      )
                    : FilledButton.tonal(
                        onPressed: () => _ble.requestPage(index, '/'),
                        child: const Text('Browse'),
                      ),
              ),
              if (isLoading)
                Padding(
                  padding: const EdgeInsets.fromLTRB(16, 0, 16, 12),
                  child: ClipRRect(
                    borderRadius: BorderRadius.circular(4),
                    child: LinearProgressIndicator(
                      value: _loadingProgress > 0 ? _loadingProgress : null,
                      minHeight: 6,
                    ),
                  ),
                ),
            ],
          ),
        );
      },
    );
  }

  Widget _buildCompanionsTab(ThemeData theme) {
    return Column(
      children: [
        // Expandable companion list
        Theme(
          data: theme.copyWith(dividerColor: Colors.transparent),
          child: ExpansionTile(
            leading: Badge(
              label: Text('${_companions.length}'),
              isLabelVisible: _companions.isNotEmpty,
              child: Icon(Icons.people,
                  color: theme.colorScheme.primary),
            ),
            title: Text(
              _companions.isEmpty
                  ? 'No companions discovered'
                  : 'Companions',
              style: theme.textTheme.titleSmall,
            ),
            subtitle: _companions.isNotEmpty
                ? Text('Tap to message',
                    style: theme.textTheme.bodySmall?.copyWith(
                        color: theme.colorScheme.onSurfaceVariant))
                : null,
            initiallyExpanded: false,
            children: [
              for (final comp in _companions)
                ListTile(
                  dense: true,
                  leading: Stack(
                    children: [
                      CircleAvatar(
                        radius: 18,
                        backgroundColor:
                            theme.colorScheme.primaryContainer,
                        child: Icon(Icons.person, size: 20,
                            color:
                                theme.colorScheme.onPrimaryContainer),
                      ),
                      Positioned(
                        right: 0,
                        bottom: 0,
                        child: Container(
                          width: 10,
                          height: 10,
                          decoration: BoxDecoration(
                            color: comp.status == 0
                                ? Colors.green
                                : Colors.grey,
                            shape: BoxShape.circle,
                            border: Border.all(
                                color: theme.colorScheme.surface,
                                width: 1.5),
                          ),
                        ),
                      ),
                    ],
                  ),
                  title: Text(comp.name),
                  subtitle: Text('ID: ${comp.id}',
                      style: theme.textTheme.bodySmall?.copyWith(
                          color: theme.colorScheme.onSurfaceVariant)),
                  trailing: Badge(
                    label: Text(
                        '${_ble.unreadDmCount(comp.id)}'),
                    isLabelVisible:
                        _ble.unreadDmCount(comp.id) > 0,
                    child: const Icon(Icons.chat_bubble_outline,
                        size: 20),
                  ),
                  onTap: () {
                    _ble.markDmRead(comp.id);
                    Navigator.push(
                      context,
                      MaterialPageRoute(
                        builder: (_) =>
                            _DMChatScreen(companion: comp),
                      ),
                    );
                  },
                ),
            ],
          ),
        ),
        const Divider(height: 1),
        // Public channel header
        Padding(
          padding: const EdgeInsets.fromLTRB(16, 8, 16, 4),
          child: Row(
            children: [
              Icon(Icons.public, size: 16,
                  color: theme.colorScheme.onSurfaceVariant),
              const SizedBox(width: 4),
              Text('Public Channel',
                  style: theme.textTheme.labelMedium?.copyWith(
                      color: theme.colorScheme.onSurfaceVariant)),
            ],
          ),
        ),
        // Public channel messages
        Expanded(
          child: _broadcastMessages.isEmpty
              ? Center(
                  child: Column(
                    mainAxisSize: MainAxisSize.min,
                    children: [
                      Icon(Icons.chat_bubble_outline, size: 48,
                          color: theme.colorScheme.onSurfaceVariant),
                      const SizedBox(height: 8),
                      Text('No messages yet',
                          style: theme.textTheme.bodyMedium?.copyWith(
                              color: theme.colorScheme.onSurfaceVariant)),
                    ],
                  ),
                )
              : ListView.builder(
                  reverse: true,
                  padding: const EdgeInsets.symmetric(
                      horizontal: 12, vertical: 4),
                  itemCount: _broadcastMessages.length,
                  itemBuilder: (context, index) {
                    final msg = _broadcastMessages[
                        _broadcastMessages.length - 1 - index];
                    return _buildChatBubble(theme, msg);
                  },
                ),
        ),
        // Broadcast input
        Padding(
          padding: const EdgeInsets.all(8.0),
          child: _MessageInput(
            hint: 'Broadcast message...',
            onSend: (text) => _ble.sendBroadcast(text),
          ),
        ),
      ],
    );
  }

  Widget _buildChatBubble(ThemeData theme, ChatMessage msg) {
    return Align(
      alignment: msg.isSelf ? Alignment.centerRight : Alignment.centerLeft,
      child: Container(
        margin: const EdgeInsets.symmetric(vertical: 2),
        padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
        constraints: BoxConstraints(
            maxWidth: MediaQuery.of(context).size.width * 0.75),
        decoration: BoxDecoration(
          color: msg.isSelf
              ? theme.colorScheme.primary
              : theme.colorScheme.surfaceContainerHighest,
          borderRadius: BorderRadius.circular(16),
        ),
        child: Column(
          crossAxisAlignment:
              msg.isSelf ? CrossAxisAlignment.end : CrossAxisAlignment.start,
          children: [
            if (!msg.isSelf)
              Text(
                msg.fromName,
                style: TextStyle(
                  fontSize: 11,
                  fontWeight: FontWeight.w600,
                  color: theme.colorScheme.primary,
                ),
              ),
            Text(
              msg.message,
              style: TextStyle(
                color: msg.isSelf
                    ? theme.colorScheme.onPrimary
                    : theme.colorScheme.onSurface,
              ),
            ),
            const SizedBox(height: 2),
            Text(
              _formatTime(msg.timestamp),
              style: TextStyle(
                fontSize: 10,
                color: msg.isSelf
                    ? theme.colorScheme.onPrimary.withValues(alpha: 0.7)
                    : theme.colorScheme.onSurfaceVariant,
              ),
            ),
          ],
        ),
      ),
    );
  }

  String _formatTime(DateTime dt) {
    return '${dt.hour.toString().padLeft(2, '0')}:${dt.minute.toString().padLeft(2, '0')}';
  }

  Widget _buildLogTab(ThemeData theme) {
    return Column(
      children: [
        Expanded(
          child: ListView.builder(
            reverse: true,
            itemCount: _logLines.length,
            itemBuilder: (context, index) {
              final line = _logLines[_logLines.length - 1 - index];
              final isEvent = line.startsWith('EVT:');
              final isCommand = line.startsWith('>');
              final isError = line.contains('ERROR');
              return Padding(
                padding:
                    const EdgeInsets.symmetric(horizontal: 12, vertical: 1),
                child: Text(
                  line,
                  style: TextStyle(
                    fontFamily: 'monospace',
                    fontSize: 12,
                    color: isError
                        ? Colors.red
                        : isCommand
                            ? theme.colorScheme.primary
                            : isEvent
                                ? theme.colorScheme.tertiary
                                : theme.colorScheme.onSurface,
                  ),
                ),
              );
            },
          ),
        ),
        Padding(
          padding: const EdgeInsets.all(8.0),
          child: _MessageInput(
            hint: 'Send command...',
            onSend: (cmd) => _ble.sendCommand(cmd),
          ),
        ),
      ],
    );
  }
}

// ============================================================
// PAGE VIEWER - Renders HTML content from getpage
// ============================================================

class _PageViewerScreen extends StatefulWidget {
  final String html;
  const _PageViewerScreen({required this.html});

  @override
  State<_PageViewerScreen> createState() => _PageViewerScreenState();
}

class _PageViewerScreenState extends State<_PageViewerScreen> {
  late final WebViewController _controller;

  @override
  void initState() {
    super.initState();
    _controller = WebViewController()
      ..setJavaScriptMode(JavaScriptMode.unrestricted)
      ..loadHtmlString(widget.html);
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('MeshWeb Page'),
        actions: [
          IconButton(
            icon: const Icon(Icons.code),
            tooltip: 'View Source',
            onPressed: () {
              showDialog(
                context: context,
                builder: (_) => AlertDialog(
                  title: const Text('Page Source'),
                  content: SingleChildScrollView(
                    child: SelectableText(
                      widget.html,
                      style: const TextStyle(
                          fontFamily: 'monospace', fontSize: 11),
                    ),
                  ),
                  actions: [
                    TextButton(
                      onPressed: () => Navigator.pop(context),
                      child: const Text('Close'),
                    ),
                  ],
                ),
              );
            },
          ),
        ],
      ),
      body: WebViewWidget(controller: _controller),
    );
  }
}

// ============================================================
// DM CHAT SCREEN - Direct message with a companion
// ============================================================

class _DMChatScreen extends StatefulWidget {
  final MeshWebCompanion companion;
  const _DMChatScreen({required this.companion});

  @override
  State<_DMChatScreen> createState() => _DMChatScreenState();
}

class _DMChatScreenState extends State<_DMChatScreen> {
  final _ble = BleService();
  List<ChatMessage> _messages = [];
  StreamSubscription? _chatSub;

  @override
  void initState() {
    super.initState();
    _messages = _ble.dmMessages(widget.companion.id).toList();
    _ble.markDmRead(widget.companion.id);
    _chatSub = _ble.chatStream.listen((msg) {
      if (mounted) {
        setState(() {
          _messages = _ble.dmMessages(widget.companion.id).toList();
          _ble.markDmRead(widget.companion.id);
        });
      }
    });
  }

  @override
  void dispose() {
    _chatSub?.cancel();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return Scaffold(
      appBar: AppBar(
        title: Row(
          children: [
            CircleAvatar(
              radius: 16,
              backgroundColor: theme.colorScheme.primaryContainer,
              child: Icon(Icons.person, size: 18,
                  color: theme.colorScheme.onPrimaryContainer),
            ),
            const SizedBox(width: 8),
            Text(widget.companion.name),
          ],
        ),
      ),
      body: Column(
        children: [
          Expanded(
            child: _messages.isEmpty
                ? Center(
                    child: Text('No messages yet',
                        style: theme.textTheme.bodyLarge?.copyWith(
                            color: theme.colorScheme.onSurfaceVariant)),
                  )
                : ListView.builder(
                    reverse: true,
                    padding: const EdgeInsets.symmetric(
                        horizontal: 12, vertical: 8),
                    itemCount: _messages.length,
                    itemBuilder: (context, index) {
                      final msg =
                          _messages[_messages.length - 1 - index];
                      return _buildBubble(theme, msg);
                    },
                  ),
          ),
          Padding(
            padding: const EdgeInsets.all(8.0),
            child: _MessageInput(
              hint: 'Message ${widget.companion.name}...',
              onSend: (text) =>
                  _ble.sendDirectMessage(widget.companion.id, text),
            ),
          ),
        ],
      ),
    );
  }

  Widget _buildBubble(ThemeData theme, ChatMessage msg) {
    return Align(
      alignment: msg.isSelf ? Alignment.centerRight : Alignment.centerLeft,
      child: Container(
        margin: const EdgeInsets.symmetric(vertical: 2),
        padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
        constraints: BoxConstraints(
            maxWidth: MediaQuery.of(context).size.width * 0.75),
        decoration: BoxDecoration(
          color: msg.isSelf
              ? theme.colorScheme.primary
              : theme.colorScheme.surfaceContainerHighest,
          borderRadius: BorderRadius.circular(16),
        ),
        child: Column(
          crossAxisAlignment:
              msg.isSelf ? CrossAxisAlignment.end : CrossAxisAlignment.start,
          children: [
            Text(
              msg.message,
              style: TextStyle(
                color: msg.isSelf
                    ? theme.colorScheme.onPrimary
                    : theme.colorScheme.onSurface,
              ),
            ),
            const SizedBox(height: 2),
            Text(
              _formatTime(msg.timestamp),
              style: TextStyle(
                fontSize: 10,
                color: msg.isSelf
                    ? theme.colorScheme.onPrimary.withValues(alpha: 0.7)
                    : theme.colorScheme.onSurfaceVariant,
              ),
            ),
          ],
        ),
      ),
    );
  }

  String _formatTime(DateTime dt) {
    return '${dt.hour.toString().padLeft(2, '0')}:${dt.minute.toString().padLeft(2, '0')}';
  }
}

// ============================================================
// REUSABLE MESSAGE INPUT WIDGET
// ============================================================

class _MessageInput extends StatefulWidget {
  final String hint;
  final Function(String) onSend;

  const _MessageInput({this.hint = 'Send message...', required this.onSend});

  @override
  State<_MessageInput> createState() => _MessageInputState();
}

class _MessageInputState extends State<_MessageInput> {
  final _controller = TextEditingController();

  void _send() {
    final text = _controller.text.trim();
    if (text.isEmpty) return;
    widget.onSend(text);
    _controller.clear();
  }

  @override
  void dispose() {
    _controller.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Row(
      children: [
        Expanded(
          child: TextField(
            controller: _controller,
            decoration: InputDecoration(
              hintText: widget.hint,
              border: const OutlineInputBorder(),
              contentPadding:
                  const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
              isDense: true,
            ),
            onSubmitted: (_) => _send(),
          ),
        ),
        const SizedBox(width: 8),
        IconButton.filled(
          onPressed: _send,
          icon: const Icon(Icons.send),
        ),
      ],
    );
  }
}
