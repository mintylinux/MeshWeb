#pragma once

#include <Arduino.h>

// Web Protocol Message Types
// These go inside RAW_CUSTOM packets (PAYLOAD_TYPE_RAW_CUSTOM = 0x0F)
#define WEB_MSG_PAGE_ANNOUNCE    0x01  // Broadcast: node announces hosted pages
#define WEB_MSG_PAGE_REQUEST     0x02  // Request: ask for a specific page
#define WEB_MSG_PAGE_DATA        0x03  // Response: page data chunk
#define WEB_MSG_SEARCH_QUERY     0x04  // Broadcast: search for content
#define WEB_MSG_SEARCH_RESULT    0x05  // Response: search results
#define WEB_MSG_COMPANION_ANNOUNCE 0x10  // Broadcast: companion announces presence
#define WEB_MSG_COMPANION_MESSAGE  0x11  // Direct message between companions
#define WEB_MSG_COMPANION_FILE     0x12  // File transfer between companions

// Maximum sizes
#define WEB_MAX_PATH_LEN         32    // Max length of page path ("/index.html")
#define WEB_MAX_CHUNK_SIZE       180   // Max data per chunk (keep packet < 256 bytes)
#define WEB_MAX_PAGES_ANNOUNCE   8     // Max pages in announce message

// PAGE_ANNOUNCE packet structure
// Sent periodically by web nodes to advertise hosted content
struct WebPageAnnounce {
  uint8_t msg_type;              // WEB_MSG_PAGE_ANNOUNCE
  uint8_t node_id[4];            // Node identifier (first 4 bytes of pub key)
  char node_name[32];            // Friendly name for the node
  uint8_t page_count;            // Number of pages hosted
  uint32_t timestamp;            // Last update time
  char pages[WEB_MAX_PAGES_ANNOUNCE][WEB_MAX_PATH_LEN];  // List of page paths
  
  // Serialize to buffer
  int writeTo(uint8_t* buf) {
    int pos = 0;
    buf[pos++] = msg_type;
    memcpy(&buf[pos], node_id, 4); pos += 4;
    strncpy((char*)&buf[pos], node_name, 32);
    pos += 32;
    buf[pos++] = page_count;
    memcpy(&buf[pos], &timestamp, 4); pos += 4;
    for (int i = 0; i < page_count && i < WEB_MAX_PAGES_ANNOUNCE; i++) {
      strncpy((char*)&buf[pos], pages[i], WEB_MAX_PATH_LEN);
      pos += WEB_MAX_PATH_LEN;
    }
    return pos;
  }
  
  // Deserialize from buffer
  bool readFrom(const uint8_t* buf, int len) {
    if (len < 42) return false;  // Updated minimum length
    int pos = 0;
    msg_type = buf[pos++];
    memcpy(node_id, &buf[pos], 4); pos += 4;
    strncpy(node_name, (char*)&buf[pos], 32);
    node_name[31] = 0;  // Ensure null termination
    pos += 32;
    page_count = buf[pos++];
    memcpy(&timestamp, &buf[pos], 4); pos += 4;
    
    if (page_count > WEB_MAX_PAGES_ANNOUNCE) page_count = WEB_MAX_PAGES_ANNOUNCE;
    for (int i = 0; i < page_count; i++) {
      if (pos + WEB_MAX_PATH_LEN > len) break;
      strncpy(pages[i], (char*)&buf[pos], WEB_MAX_PATH_LEN);
      pages[i][WEB_MAX_PATH_LEN-1] = 0; // Ensure null termination
      pos += WEB_MAX_PATH_LEN;
    }
    return true;
  }
};

// PAGE_REQUEST packet structure
// Sent by companion node to request a page
struct WebPageRequest {
  uint8_t msg_type;              // WEB_MSG_PAGE_REQUEST
  uint8_t request_id;            // Unique request ID for tracking
  uint8_t target_node[4];        // Target node to serve the page
  uint16_t chunk_index;          // Which chunk to request (0 = first)
  char page_path[WEB_MAX_PATH_LEN];  // Path to requested page
  
  int writeTo(uint8_t* buf) {
    int pos = 0;
    buf[pos++] = msg_type;
    buf[pos++] = request_id;
    memcpy(&buf[pos], target_node, 4); pos += 4;
    memcpy(&buf[pos], &chunk_index, 2); pos += 2;
    strncpy((char*)&buf[pos], page_path, WEB_MAX_PATH_LEN);
    pos += WEB_MAX_PATH_LEN;
    return pos;
  }
  
  bool readFrom(const uint8_t* buf, int len) {
    if (len < 8 + WEB_MAX_PATH_LEN) return false;
    int pos = 0;
    msg_type = buf[pos++];
    request_id = buf[pos++];
    memcpy(target_node, &buf[pos], 4); pos += 4;
    memcpy(&chunk_index, &buf[pos], 2); pos += 2;
    strncpy(page_path, (char*)&buf[pos], WEB_MAX_PATH_LEN);
    page_path[WEB_MAX_PATH_LEN-1] = 0;
    return true;
  }
};

// PAGE_DATA packet structure
// Response containing page content chunk
struct WebPageData {
  uint8_t msg_type;              // WEB_MSG_PAGE_DATA
  uint8_t request_id;            // Matching request ID
  uint16_t chunk_index;          // Current chunk number
  uint16_t total_chunks;         // Total number of chunks
  uint16_t data_len;             // Length of data in this chunk
  uint8_t data[WEB_MAX_CHUNK_SIZE];  // Actual page data
  
  int writeTo(uint8_t* buf) {
    int pos = 0;
    buf[pos++] = msg_type;
    buf[pos++] = request_id;
    memcpy(&buf[pos], &chunk_index, 2); pos += 2;
    memcpy(&buf[pos], &total_chunks, 2); pos += 2;
    memcpy(&buf[pos], &data_len, 2); pos += 2;
    if (data_len > WEB_MAX_CHUNK_SIZE) data_len = WEB_MAX_CHUNK_SIZE;
    memcpy(&buf[pos], data, data_len);
    pos += data_len;
    return pos;
  }
  
  bool readFrom(const uint8_t* buf, int len) {
    if (len < 9) return false;
    int pos = 0;
    msg_type = buf[pos++];
    request_id = buf[pos++];
    memcpy(&chunk_index, &buf[pos], 2); pos += 2;
    memcpy(&total_chunks, &buf[pos], 2); pos += 2;
    memcpy(&data_len, &buf[pos], 2); pos += 2;
    
    if (data_len > WEB_MAX_CHUNK_SIZE) return false;
    if (pos + data_len > len) return false;
    
    memcpy(data, &buf[pos], data_len);
    return true;
  }
};

// SEARCH_QUERY packet structure
struct WebSearchQuery {
  uint8_t msg_type;              // WEB_MSG_SEARCH_QUERY
  uint8_t query_id;              // Unique query ID
  char search_terms[48];         // Search keywords
  
  int writeTo(uint8_t* buf) {
    int pos = 0;
    buf[pos++] = msg_type;
    buf[pos++] = query_id;
    strncpy((char*)&buf[pos], search_terms, 48);
    pos += 48;
    return pos;
  }
  
  bool readFrom(const uint8_t* buf, int len) {
    if (len < 50) return false;
    int pos = 0;
    msg_type = buf[pos++];
    query_id = buf[pos++];
    strncpy(search_terms, (char*)&buf[pos], 48);
    search_terms[47] = 0;
    return true;
  }
};

// COMPANION_ANNOUNCE packet structure
// Sent periodically by companions to announce presence
struct WebCompanionAnnounce {
  uint8_t msg_type;              // WEB_MSG_COMPANION_ANNOUNCE
  uint8_t node_id[4];            // Companion identifier
  char name[32];                 // Friendly name
  uint8_t status;                // 0=available, 1=busy, etc.
  uint32_t timestamp;            // Uptime or last activity
  
  int writeTo(uint8_t* buf) {
    int pos = 0;
    buf[pos++] = msg_type;
    memcpy(&buf[pos], node_id, 4); pos += 4;
    strncpy((char*)&buf[pos], name, 32); pos += 32;
    buf[pos++] = status;
    memcpy(&buf[pos], &timestamp, 4); pos += 4;
    return pos;
  }
  
  bool readFrom(const uint8_t* buf, int len) {
    if (len < 42) return false;
    int pos = 0;
    msg_type = buf[pos++];
    memcpy(node_id, &buf[pos], 4); pos += 4;
    strncpy(name, (char*)&buf[pos], 32);
    name[31] = 0;
    pos += 32;
    status = buf[pos++];
    memcpy(&timestamp, &buf[pos], 4); pos += 4;
    return true;
  }
};

// COMPANION_MESSAGE packet structure
// Direct message between companions
#define WEB_MAX_MESSAGE_LEN 160
struct WebCompanionMessage {
  uint8_t msg_type;              // WEB_MSG_COMPANION_MESSAGE
  uint8_t from_id[4];            // Sender companion ID
  uint8_t to_id[4];              // Target companion ID (0xFFFFFFFF = broadcast)
  uint8_t msg_id;                // Message ID for tracking
  char message[WEB_MAX_MESSAGE_LEN];  // Message text
  
  int writeTo(uint8_t* buf) {
    int pos = 0;
    buf[pos++] = msg_type;
    memcpy(&buf[pos], from_id, 4); pos += 4;
    memcpy(&buf[pos], to_id, 4); pos += 4;
    buf[pos++] = msg_id;
    strncpy((char*)&buf[pos], message, WEB_MAX_MESSAGE_LEN);
    pos += WEB_MAX_MESSAGE_LEN;
    return pos;
  }
  
  bool readFrom(const uint8_t* buf, int len) {
    if (len < 10 + WEB_MAX_MESSAGE_LEN) return false;
    int pos = 0;
    msg_type = buf[pos++];
    memcpy(from_id, &buf[pos], 4); pos += 4;
    memcpy(to_id, &buf[pos], 4); pos += 4;
    msg_id = buf[pos++];
    strncpy(message, (char*)&buf[pos], WEB_MAX_MESSAGE_LEN);
    message[WEB_MAX_MESSAGE_LEN-1] = 0;
    return true;
  }
  
  // Check if message is broadcast
  bool isBroadcast() {
    return to_id[0] == 0xFF && to_id[1] == 0xFF && to_id[2] == 0xFF && to_id[3] == 0xFF;
  }
};

// Helper functions
namespace WebProtocol {
  
  // Parse message type from raw packet
  inline uint8_t getMessageType(const uint8_t* buf) {
    return buf[0];
  }
  
  // Check if this node ID matches
  inline bool isTargetNode(const uint8_t* target, const uint8_t* our_id) {
    return memcmp(target, our_id, 4) == 0;
  }
  
  // Extract node ID from full public key
  inline void extractNodeId(uint8_t* dest, const uint8_t* pub_key) {
    memcpy(dest, pub_key, 4);
  }
}
