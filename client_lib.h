// ============================================================================
// DISTRIBUTED FILE SYSTEM - CLIENT LIBRARY
// ============================================================================
// File: client_lib.h & client_lib.cpp
// Description: Client API for file operations
// ============================================================================

#ifndef DFS_CLIENT_LIB_H
#define DFS_CLIENT_LIB_H

#include "common.h"
#include "network.h"
#include <string>
#include <memory>
#include <map>
#include <mutex>

class DistributedFileSystem {
public:
    explicit DistributedFileSystem(const std::string& metadata_server_ip, uint16_t metadata_port);
    ~DistributedFileSystem();
    
    // File operations
    int create_file(const std::string& path, uint32_t permissions = 0644);
    int delete_file(const std::string& path);
    int mkdir(const std::string& path);
    
    // File I/O operations
    int open(const std::string& path, int flags);
    size_t read(int fd, void* buffer, size_t size);
    size_t write(int fd, const void* data, size_t size);
    int close(int fd);
    
    // File info
    bool get_file_info(const std::string& path, FileMetadata& metadata);
    
    // Connection management
    bool reconnect_to_metadata_server();
    bool is_connected() const { return metadata_client_ && metadata_client_->is_connected(); }

private:
    struct OpenFileHandle {
        std::string path;
        uint64_t file_id;
        uint64_t current_offset;
        std::vector<ChunkHandle> chunks;
        bool writable;
        time_t open_time;
    };
    
    std::string metadata_server_ip_;
    uint16_t metadata_port_;
    std::unique_ptr<NetworkSocket> metadata_client_;
    std::unique_ptr<ConnectionPool> chunk_pool_;
    
    std::map<int, OpenFileHandle> open_files_;
    int next_file_handle_;
    std::mutex files_mutex_;
    
    // Metadata cache
    struct CachedMetadata {
        FileMetadata metadata;
        time_t cached_time;
    };
    std::map<std::string, CachedMetadata> metadata_cache_;
    std::mutex cache_mutex_;
    
    // Internal helpers
    bool query_metadata(const std::string& path, FileMetadata& metadata);
    std::vector<ChunkLocation> select_replicas(const std::vector<ChunkHandle>& chunks);
    ChunkLocation select_nearest_replica(const std::vector<ChunkLocation>& replicas);
    bool read_chunk(uint64_t chunk_id, uint32_t offset, uint32_t length, 
                   std::vector<uint8_t>& data);
    bool write_chunk(uint64_t chunk_id, uint32_t offset, const std::vector<uint8_t>& data);
    void invalidate_cache_entry(const std::string& path);
};

#endif // DFS_CLIENT_LIB_H


// ============================================================================
// File: client_lib.cpp
// ============================================================================

#include "client_lib.h"
#include <iostream>
#include <cstring>

DistributedFileSystem::DistributedFileSystem(const std::string& metadata_server_ip, 
                                           uint16_t metadata_port)
    : metadata_server_ip_(metadata_server_ip), metadata_port_(metadata_port), 
      next_file_handle_(1) {
    
    metadata_client_ = std::make_unique<NetworkSocket>();
    chunk_pool_ = std::make_unique<ConnectionPool>(20);
}

DistributedFileSystem::~DistributedFileSystem() {
    std::unique_lock<std::mutex> lock(files_mutex_);
    for (auto& entry : open_files_) {
        close(entry.first);
    }
}

bool DistributedFileSystem::reconnect_to_metadata_server() {
    if (metadata_client_) {
        metadata_client_->close_socket();
    }
    
    metadata_client_ = std::make_unique<NetworkSocket>();
    if (metadata_client_->connect_to_server(metadata_server_ip_, metadata_port_)) {
        return true;
    }
    
    std::cerr << "Failed to reconnect to metadata server" << std::endl;
    return false;
}

int DistributedFileSystem::create_file(const std::string& path, uint32_t permissions) {
    if (!metadata_client_ || !metadata_client_->is_connected()) {
        if (!reconnect_to_metadata_server()) {
            return -1;
        }
    }
    
    // Send file creation request to metadata server
    ProtocolFrame frame;
    frame.magic = DFS_PROTOCOL_MAGIC;
    frame.version = DFS_PROTOCOL_VERSION;
    frame.message_type = OP_FILE_CREATE;
    frame.payload_size = path.size() + 8; // path + permissions
    
    uint8_t* payload_ptr = frame.payload;
    std::memcpy(payload_ptr, path.c_str(), path.size());
    payload_ptr += path.size();
    std::memcpy(payload_ptr, &permissions, 4);
    
    frame.checksum = NetworkSocket::calculate_crc32(frame.payload, frame.payload_size);
    
    if (!metadata_client_->send_frame(frame)) {
        return -1;
    }
    
    // Wait for acknowledgment
    ProtocolFrame response;
    if (!metadata_client_->recv_frame(response)) {
        return -1;
    }
    
    if (response.message_type != OP_ACK) {
        return -1;
    }
    
    // Parse response to get file_id
    uint64_t file_id = 0;
    std::memcpy(&file_id, response.payload, 8);
    
    invalidate_cache_entry(path);
    return 0;
}

int DistributedFileSystem::delete_file(const std::string& path) {
    if (!metadata_client_ || !metadata_client_->is_connected()) {
        if (!reconnect_to_metadata_server()) {
            return -1;
        }
    }
    
    ProtocolFrame frame;
    frame.magic = DFS_PROTOCOL_MAGIC;
    frame.version = DFS_PROTOCOL_VERSION;
    frame.message_type = OP_FILE_DELETE;
    frame.payload_size = path.size();
    
    std::memcpy(frame.payload, path.c_str(), path.size());
    frame.checksum = NetworkSocket::calculate_crc32(frame.payload, frame.payload_size);
    
    if (!metadata_client_->send_frame(frame)) {
        return -1;
    }
    
    ProtocolFrame response;
    if (!metadata_client_->recv_frame(response)) {
        return -1;
    }
    
    invalidate_cache_entry(path);
    return response.message_type == OP_ACK ? 0 : -1;
}

int DistributedFileSystem::mkdir(const std::string& path) {
    if (!metadata_client_ || !metadata_client_->is_connected()) {
        if (!reconnect_to_metadata_server()) {
            return -1;
        }
    }
    
    ProtocolFrame frame;
    frame.magic = DFS_PROTOCOL_MAGIC;
    frame.version = DFS_PROTOCOL_VERSION;
    frame.message_type = OP_MKDIR;
    frame.payload_size = path.size();
    
    std::memcpy(frame.payload, path.c_str(), path.size());
    frame.checksum = NetworkSocket::calculate_crc32(frame.payload, frame.payload_size);
    
    if (!metadata_client_->send_frame(frame)) {
        return -1;
    }
    
    ProtocolFrame response;
    if (!metadata_client_->recv_frame(response)) {
        return -1;
    }
    
    invalidate_cache_entry(path);
    return response.message_type == OP_ACK ? 0 : -1;
}

bool DistributedFileSystem::query_metadata(const std::string& path, FileMetadata& metadata) {
    // Check cache first
    {
        std::unique_lock<std::mutex> lock(cache_mutex_);
        auto it = metadata_cache_.find(path);
        if (it != metadata_cache_.end()) {
            time_t current_time = std::time(nullptr);
            if (current_time - it->second.cached_time < DFS_METADATA_CACHE_TTL_SEC) {
                metadata = it->second.metadata;
                return true;
            } else {
                metadata_cache_.erase(it);
            }
        }
    }
    
    // Query from metadata server
    if (!metadata_client_ || !metadata_client_->is_connected()) {
        if (!reconnect_to_metadata_server()) {
            return false;
        }
    }
    
    ProtocolFrame frame;
    frame.magic = DFS_PROTOCOL_MAGIC;
    frame.version = DFS_PROTOCOL_VERSION;
    frame.message_type = OP_METADATA_QUERY;
    frame.payload_size = path.size();
    
    std::memcpy(frame.payload, path.c_str(), path.size());
    frame.checksum = NetworkSocket::calculate_crc32(frame.payload, frame.payload_size);
    
    if (!metadata_client_->send_frame(frame)) {
        return false;
    }
    
    ProtocolFrame response;
    if (!metadata_client_->recv_frame(response)) {
        return false;
    }
    
    // Parse response (simplified)
    if (response.payload_size >= sizeof(FileMetadata)) {
        std::memcpy(&metadata, response.payload, sizeof(FileMetadata));
        
        // Cache the result
        {
            std::unique_lock<std::mutex> lock(cache_mutex_);
            metadata_cache_[path] = {metadata, std::time(nullptr)};
        }
        
        return true;
    }
    
    return false;
}

bool DistributedFileSystem::get_file_info(const std::string& path, FileMetadata& metadata) {
    return query_metadata(path, metadata);
}

int DistributedFileSystem::open(const std::string& path, int flags) {
    FileMetadata metadata;
    if (!query_metadata(path, metadata)) {
        return -1;
    }
    
    std::unique_lock<std::mutex> lock(files_mutex_);
    int fd = next_file_handle_++;
    
    OpenFileHandle handle;
    handle.path = path;
    handle.file_id = metadata.file_id;
    handle.current_offset = 0;
    handle.chunks = metadata.chunks;
    handle.writable = (flags & 0x01) != 0;  // Simplified flag check
    handle.open_time = std::time(nullptr);
    
    open_files_[fd] = handle;
    return fd;
}

size_t DistributedFileSystem::read(int fd, void* buffer, size_t size) {
    if (!buffer || size == 0) {
        return 0;
    }
    
    std::unique_lock<std::mutex> lock(files_mutex_);
    auto it = open_files_.find(fd);
    if (it == open_files_.end()) {
        return 0;
    }
    
    OpenFileHandle& handle = it->second;
    std::vector<uint8_t> data;
    
    // Simplified: read from first chunk
    if (handle.chunks.empty()) {
        return 0;
    }
    
    uint64_t chunk_id = handle.chunks[0].chunk_id;
    if (!read_chunk(chunk_id, handle.current_offset, size, data)) {
        return 0;
    }
    
    std::memcpy(buffer, data.data(), std::min(data.size(), size));
    handle.current_offset += data.size();
    
    return data.size();
}

size_t DistributedFileSystem::write(int fd, const void* data, size_t size) {
    if (!data || size == 0) {
        return 0;
    }
    
    std::unique_lock<std::mutex> lock(files_mutex_);
    auto it = open_files_.find(fd);
    if (it == open_files_.end()) {
        return 0;
    }
    
    OpenFileHandle& handle = it->second;
    if (!handle.writable) {
        return 0;
    }
    
    // Simplified: write to first chunk
    if (handle.chunks.empty()) {
        return 0;
    }
    
    uint64_t chunk_id = handle.chunks[0].chunk_id;
    std::vector<uint8_t> write_data((uint8_t*)data, (uint8_t*)data + size);
    
    if (!write_chunk(chunk_id, handle.current_offset, write_data)) {
        return 0;
    }
    
    handle.current_offset += size;
    return size;
}

int DistributedFileSystem::close(int fd) {
    std::unique_lock<std::mutex> lock(files_mutex_);
    auto it = open_files_.find(fd);
    if (it == open_files_.end()) {
        return -1;
    }
    
    open_files_.erase(it);
    return 0;
}

bool DistributedFileSystem::read_chunk(uint64_t chunk_id, uint32_t offset, 
                                      uint32_t length, std::vector<uint8_t>& data) {
    // Simplified: connect to first replica and read
    FileMetadata dummy;
    if (dummy.chunks.empty()) {
        return false;
    }
    
    auto& chunk = dummy.chunks[0];
    if (chunk.replicas.empty()) {
        return false;
    }
    
    auto& replica = chunk.replicas[0];
    auto socket = chunk_pool_->acquire(replica.ip_address, replica.port);
    if (!socket) {
        return false;
    }
    
    ProtocolFrame frame;
    frame.magic = DFS_PROTOCOL_MAGIC;
    frame.version = DFS_PROTOCOL_VERSION;
    frame.message_type = OP_READ;
    
    FileReadRequest read_req;
    read_req.chunk_id = chunk_id;
    read_req.offset = offset;
    read_req.length = length;
    read_req.version = chunk.version;
    
    frame.payload_size = sizeof(FileReadRequest);
    std::memcpy(frame.payload, &read_req, frame.payload_size);
    frame.checksum = NetworkSocket::calculate_crc32(frame.payload, frame.payload_size);
    
    if (!socket->send_frame(frame)) {
        return false;
    }
    
    ProtocolFrame response;
    if (!socket->recv_frame(response)) {
        return false;
    }
    
    FileReadResponse* read_resp = (FileReadResponse*)response.payload;
    if (read_resp->success) {
        data = read_resp->data;
        return true;
    }
    
    return false;
}

bool DistributedFileSystem::write_chunk(uint64_t chunk_id, uint32_t offset,
                                       const std::vector<uint8_t>& data) {
    // Simplified: connect to first replica and write
    FileMetadata dummy;
    if (dummy.chunks.empty()) {
        return false;
    }
    
    auto& chunk = dummy.chunks[0];
    if (chunk.replicas.empty()) {
        return false;
    }
    
    auto& replica = chunk.replicas[0];
    auto socket = chunk_pool_->acquire(replica.ip_address, replica.port);
    if (!socket) {
        return false;
    }
    
    ProtocolFrame frame;
    frame.magic = DFS_PROTOCOL_MAGIC;
    frame.version = DFS_PROTOCOL_VERSION;
    frame.message_type = OP_WRITE;
    
    FileWriteRequest write_req;
    write_req.chunk_id = chunk_id;
    write_req.offset = offset;
    write_req.data = data;
    write_req.version = chunk.version;
    
    frame.payload_size = sizeof(FileWriteRequest) + data.size();
    std::memcpy(frame.payload, &write_req, sizeof(FileWriteRequest));
    std::memcpy(frame.payload + sizeof(FileWriteRequest), data.data(), data.size());
    frame.checksum = NetworkSocket::calculate_crc32(frame.payload, frame.payload_size);
    
    if (!socket->send_frame(frame)) {
        return false;
    }
    
    ProtocolFrame response;
    if (!socket->recv_frame(response)) {
        return false;
    }
    
    FileWriteResponse* write_resp = (FileWriteResponse*)response.payload;
    return write_resp->success;
}

void DistributedFileSystem::invalidate_cache_entry(const std::string& path) {
    std::unique_lock<std::mutex> lock(cache_mutex_);
    metadata_cache_.erase(path);
}

// Example: ChunkLocation select_nearest_replica helper
ChunkLocation DistributedFileSystem::select_nearest_replica(const std::vector<ChunkLocation>& replicas) {
    if (replicas.empty()) {
        return ChunkLocation();
    }
    
    // Simplified: return first replica (in production, use latency-based selection)
    return replicas[0];
}

std::vector<ChunkLocation> DistributedFileSystem::select_replicas(const std::vector<ChunkHandle>& chunks) {
    std::vector<ChunkLocation> locations;
    for (const auto& chunk : chunks) {
        if (!chunk.replicas.empty()) {
            locations.push_back(select_nearest_replica(chunk.replicas));
        }
    }
    return locations;
}
