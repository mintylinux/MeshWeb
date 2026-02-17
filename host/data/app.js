// MeshWeb Search Engine
// This is a demo - in production, this would query real mesh nodes

// Sample database of pages (would come from mesh network)
const samplePages = [
    {
        title: "Welcome to MeshWeb",
        nodeId: "ff1a2b3c",
        description: "Learn about the decentralized mesh network and how it works.",
        hops: 1,
        timestamp: Date.now() - 120000, // 2 min ago
        keywords: ["meshweb", "welcome", "introduction", "guide"]
    },
    {
        title: "Mesh Network News",
        nodeId: "8d4e5f6g",
        description: "Latest updates and announcements from the mesh community.",
        hops: 3,
        timestamp: Date.now() - 900000, // 15 min ago
        keywords: ["news", "updates", "announcements", "community"]
    },
    {
        title: "Off-Grid Chat",
        nodeId: "7c8d9e0f",
        description: "Real-time messaging board for the local mesh network.",
        hops: 2,
        timestamp: Date.now() - 300000, // 5 min ago
        keywords: ["chat", "messaging", "communication", "talk"]
    },
    {
        title: "DIY Projects",
        nodeId: "1a2b3c4d",
        description: "Share your maker projects, 3D prints, and tech builds.",
        hops: 4,
        timestamp: Date.now() - 3600000, // 1 hour ago
        keywords: ["diy", "maker", "projects", "3d printing", "tech"]
    },
    {
        title: "Local Weather Station",
        nodeId: "5e6f7g8h",
        description: "Real-time weather data from nearby sensors on the mesh.",
        hops: 1,
        timestamp: Date.now() - 30000, // 30 sec ago
        keywords: ["weather", "temperature", "sensors", "data"]
    },
    {
        title: "Emergency Notices",
        nodeId: "9f0a1b2c",
        description: "Important alerts and emergency information for the local area.",
        hops: 2,
        timestamp: Date.now() - 1800000, // 30 min ago
        keywords: ["emergency", "alerts", "important", "notices"]
    }
];

// Search functionality
function performSearch(query) {
    if (!query || query.trim() === '') {
        showPopularPages();
        return;
    }

    const searchTerms = query.toLowerCase().split(' ');
    const results = samplePages.filter(page => {
        const searchText = (page.title + ' ' + page.description + ' ' + page.keywords.join(' ')).toLowerCase();
        return searchTerms.some(term => searchText.includes(term));
    });

    displaySearchResults(query, results);
}

function displaySearchResults(query, results) {
    const container = document.getElementById('results-container');
    
    if (results.length === 0) {
        container.innerHTML = `
            <div class="no-results">
                <h3>No results found for "${query}"</h3>
                <p>Try different keywords or browse recently discovered pages below.</p>
            </div>
        `;
        setTimeout(showPopularPages, 3000);
        return;
    }

    let html = `
        <h2>🔍 Search Results for "${query}"</h2>
        <p style="color: #666; margin-bottom: 20px;">${results.length} page${results.length > 1 ? 's' : ''} found</p>
        <div id="page-list">
    `;

    results.forEach(page => {
        html += createPageCard(page);
    });

    html += '</div>';
    container.innerHTML = html;
}

function createPageCard(page) {
    return `
        <div class="page-card" onclick="openPage('${page.nodeId}', '${page.title}')">
            <div class="page-header">
                <h3>${page.title}</h3>
                <span class="node-id">Node: ${page.nodeId}</span>
            </div>
            <p class="page-description">${page.description}</p>
            <div class="page-meta">
                <span>📍 ${page.hops} hop${page.hops > 1 ? 's' : ''} away</span>
                <span>🕐 ${formatTime(page.timestamp)}</span>
            </div>
        </div>
    `;
}

function showPopularPages() {
    const container = document.getElementById('results-container');
    let html = `
        <h2>📡 Recently Discovered Pages</h2>
        <div id="page-list">
    `;

    samplePages.forEach(page => {
        html += createPageCard(page);
    });

    html += '</div>';
    container.innerHTML = html;
}

function openPage(nodeId, title) {
    alert(`Opening "${title}" from node ${nodeId}\n\nIn the full version, this would:\n1. Send request via LoRa to node ${nodeId}\n2. Receive page content\n3. Display in browser`);
}

function formatTime(timestamp) {
    const diff = Date.now() - timestamp;
    const minutes = Math.floor(diff / 60000);
    const hours = Math.floor(diff / 3600000);
    
    if (minutes < 1) return 'Just now';
    if (minutes < 60) return `${minutes} min ago`;
    if (hours < 24) return `${hours} hour${hours > 1 ? 's' : ''} ago`;
    return new Date(timestamp).toLocaleDateString();
}

function showAbout() {
    alert(`MeshWeb Search Engine

A decentralized search engine running on LoRa mesh networks.

Features:
• Discover pages across the mesh network
• No internet required
• Fully decentralized
• Privacy-focused

Pages are hosted on individual nodes and indexed automatically.`);
}

function showHostInfo() {
    alert(`Host Your Own Page

To host a page on the mesh network:

1. Set up a MeshWeb node (ESP32 + LoRa)
2. Create HTML files in the data/ folder
3. Upload to SPIFFS filesystem
4. Your page will be automatically discovered!

Storage: ~1.5MB per node
Range: 2-10km per hop`);
}

// Event listeners
document.addEventListener('DOMContentLoaded', () => {
    const searchInput = document.getElementById('search-input');
    const searchBtn = document.getElementById('search-btn');

    // Search on button click
    searchBtn.addEventListener('click', () => {
        performSearch(searchInput.value);
    });

    // Search on Enter key
    searchInput.addEventListener('keypress', (e) => {
        if (e.key === 'Enter') {
            performSearch(searchInput.value);
        }
    });

    // Show popular pages on load
    showPopularPages();

    console.log('MeshWeb Search Engine initialized');
    console.log('Ready to search ' + samplePages.length + ' pages across the mesh network');
});
