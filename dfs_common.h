// ============================================================================
// DISTRIBUTED FILE SYSTEM - COMPLETE C/C++ IMPLEMENTATION
// ============================================================================
// File: common.h
// Description: Common data structures and protocol definitions
// ============================================================================

#ifndef DFS_COMMON_H
#define DFS_COMMON_H

#include <cstdint>
#include <string>
#include <vector>
#include <ctime>
#include <map>

// ============================================================================
// CONFIGURATION PARAMETERS
// ============================================================================

const int DFS_CHUNK_SIZE_MB = 64;
const int DFS_REPLICATION_FACTOR = 3;
const int DFS_MINIMUM_REPLICAS = 2;
const int DFS_HEARTBEAT_INTERVAL_SEC = 3;
const int DFS_HEARTBEAT_TIMEOUT_SEC = 60;
const int DFS_REPLICATION_TIMEOUT_SEC = 600;
const int DFS_RECOVERY_PARALLELISM = 5;
const int DFS_METADATA_CACHE_TTL_SEC = 300;
const int DFS_CLIENT_CACHE_SIZE_MB = 100;
const int DFS_MAX_CONCURRENT_CLIENTS = 1000;
const int DFS_NETWORK_TIMEOUT_MS = 5000;
const int DFS_RETRY_ATTEMPTS = 3;
const int DFS_RETRY_BACKOFF_MS = 100;

const uint32_t DFS_CHUNK_SIZE_BYTES = DFS_CHUNK_SIZE_MB * 1024 * 1024;
const uint32_t DFS_PROTOCOL_MAGIC = 0xDEADBEEF;
const uint16_t DFS_PROTOCOL_VERSION = 1;

// ============================================================================
// MESSAGE TYPES
// ============================================================================

enum MessageType : uint16_t {
    OP_READ = 0x01,
    OP_WRITE = 0x02,
    OP_DELETE = 0x03,
    OP_REPLICATE = 0x04,
    OP_HEARTBEAT = 0x05,
    OP_METADATA_QUERY = 0x06,
    OP_FILE_CREATE = 0x07,
    OP_FILE_DELETE = 0x08,
    OP_MKDIR = 0x09,
    OP_ACK = 0xFF
};

// ============================================================================
// DATA STRUCTURES
// ============================================================================

// Chunk handle and location tracking
struct ChunkLocation {
    std::string server_id;
    std::string ip_address;
    uint16_t port;
    uint64_t generation_number;
    
    ChunkLocation() : port(0), generation_number(0) {}
    ChunkLocation(const std::string& id, const std::string& ip, uint16_t p, uint64_t gen)
        : server_id(id), ip_address(ip), port(p), generation_number(gen) {}
};

struct ChunkHandle {
    uint64_t chunk_id;
    std::vector<ChunkLocation> replicas;
    uint32_t version;
    uint64_t creation_time;
    uint64_t size;
    
    ChunkHandle() : chunk_id(0), version(0), creation_time(0), size(0) {}
};

// File metadata
struct FileMetadata {
    std::string path;
    uint64_t file_id;
    uint32_t permissions;
    uint64_t creation_time;
    uint64_t modification_time;
    uint64_t file_size;
    std::vector<ChunkHandle> chunks;
    uint32_t replication_factor;
    std::string owner;
    bool is_directory;
    
    FileMetadata() 
        : file_id(0), permissions(0644), file_size(0), 
          replication_factor(DFS_REPLICATION_FACTOR), is_directory(false) {
        creation_time = modification_time = std::time(nullptr);
    }
};

// Chunk server health status
struct ChunkServerStatus {
    std::string server_id;
    std::string ip_address;
    uint16_t port;
    uint64_t total_capacity_bytes;
    uint64_t used_capacity_bytes;
    std::vector<uint64_t> healthy_chunks;
    uint32_t replication_queue_length;
    time_t last_heartbeat;
    bool is_healthy;
    
    ChunkServerStatus() 
        : port(0), total_capacity_bytes(0), used_capacity_bytes(0), 
          replication_queue_length(0), is_healthy(true) {
        last_heartbeat = std::time(nullptr);
    }
};

// Heartbeat message structure
struct HeartbeatMessage {
    std::string server_id;
    uint64_t timestamp;
    std::vector<uint64_t> healthy_chunks;
    uint64_t total_capacity;
    uint64_t used_capacity;
    uint32_t replication_queue_length;
};

// Protocol frame structure (network layer)
struct ProtocolFrame {
    uint32_t magic;              // 0xDEADBEEF
    uint16_t version;             // Protocol version
    uint16_t message_type;         // MessageType enum
    uint32_t payload_size;         // Data size
    uint32_t checksum;             // CRC32
    uint8_t payload[DFS_CHUNK_SIZE_BYTES];  // Variable length data
};

// Request/Response structures
struct FileReadRequest {
    uint64_t chunk_id;
    uint32_t offset;
    uint32_t length;
    uint32_t version;
    std::string client_id;
};

struct FileReadResponse {
    uint64_t chunk_id;
    uint32_t offset;
    uint32_t length;
    std::vector<uint8_t> data;
    bool success;
    std::string error_message;
};

struct FileWriteRequest {
    uint64_t chunk_id;
    uint32_t offset;
    std::vector<uint8_t> data;
    uint32_t version;
    std::string client_id;
};

struct FileWriteResponse {
    uint64_t chunk_id;
    bool success;
    std::string error_message;
};

struct MetadataQueryRequest {
    std::string path;
    std::string client_id;
    uint16_t operation;  // OP_READ, OP_WRITE, etc.
};

struct MetadataQueryResponse {
    std::string path;
    FileMetadata file_metadata;
    std::vector<ChunkLocation> chunk_locations;
    bool success;
    std::string error_message;
};

#endif // DFS_COMMON_H
