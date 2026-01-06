// ============================================================================
// DISTRIBUTED FILE SYSTEM - CHUNK SERVER IMPLEMENTATION
// ============================================================================
// File: chunk_server.h & chunk_server.cpp
// ============================================================================

#ifndef DFS_CHUNK_SERVER_H
#define DFS_CHUNK_SERVER_H

#include "common.h"
#include "network.h"
#include "thread_pool.h"
#include <string>
#include <map>
#include <mutex>
#include <memory>

class ChunkServer {
public:
    explicit ChunkServer(const std::string& server_id, const std::string& ip, uint16_t port,
                        const std::string& storage_path, uint64_t max_capacity);
    ~ChunkServer();
    
    // Server lifecycle
    bool start();
    void stop();
    bool is_running() const { return running_; }
    
    // Chunk operations (for testing)
    bool write_chunk(uint64_t chunk_id, const std::vector<uint8_t>& data);
    bool read_chunk(uint64_t chunk_id, std::vector<uint8_t>& data);
    bool delete_chunk(uint64_t chunk_id);
    bool replicate_chunk(uint64_t chunk_id, const std::string& target_ip, uint16_t target_port);
    
    // Health reporting
    ChunkServerStatus get_status() const;
    
private:
    struct StoredChunk {
        uint64_t chunk_id;
        std::vector<uint8_t> data;
        uint32_t version;
        uint64_t size;
        time_t creation_time;
        time_t last_access;
        uint32_t checksum;
    };
    
    std::string server_id_;
    std::string ip_;
    uint16_t port_;
    std::string storage_path_;
    uint64_t max_capacity_;
    uint64_t used_capacity_;
    bool running_;
    
    std::map<uint64_t, StoredChunk> chunks_;
    std::mutex chunks_mutex_;
    
    std::unique_ptr<NetworkSocket> server_socket_;
    std::unique_ptr<ThreadPool> thread_pool_;
    
    // Metadata server connection
    std::string metadata_server_ip_;
    uint16_t metadata_server_port_;
    std::unique_ptr<NetworkSocket> metadata_client_;
    
    // Internal methods
    void accept_connections();
    void handle_client_connection(int client_fd, const std::string& client_ip);
    void send_heartbeat();
    bool process_message(const ProtocolFrame& frame, ProtocolFrame& response);
    bool handle_read(const FileReadRequest& req, FileReadResponse& resp);
    bool handle_write(const FileWriteRequest& req, FileWriteResponse& resp);
    bool handle_replicate(const ProtocolFrame& frame);
    uint64_t get_available_capacity() const;
};

#endif // DFS_CHUNK_SERVER_H


// ============================================================================
// File: chunk_server.cpp
// ============================================================================

#include "chunk_server.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <cstring>
#include <cmath>

ChunkServer::ChunkServer(const std::string& server_id, const std::string& ip, uint16_t port,
                        const std::string& storage_path, uint64_t max_capacity)
    : server_id_(server_id), ip_(ip), port_(port), storage_path_(storage_path),
      max_capacity_(max_capacity), used_capacity_(0), running_(false),
      metadata_server_ip_("127.0.0.1"), metadata_server_port_(9000) {
    
    server_socket_ = std::make_unique<NetworkSocket>();
    thread_pool_ = std::make_unique<ThreadPool>(std::thread::hardware_concurrency());
}

ChunkServer::~ChunkServer() {
    stop();
}

bool ChunkServer::start() {
    // Create server socket
    if (!server_socket_->create_server_socket(ip_, port_)) {
        std::cerr << "Failed to create server socket on " << ip_ << ":" << port_ << std::endl;
        return false;
    }
    
    if (!server_socket_->listen_for_connections(10)) {
        std::cerr << "Failed to listen on socket" << std::endl;
        return false;
    }
    
    running_ = true;
    std::cout << "Chunk Server " << server_id_ << " started on " << ip_ << ":" << port_ << std::endl;
    
    // Start accepting connections in thread pool
    thread_pool_->enqueue([this] { accept_connections(); });
    
    // Start heartbeat thread
    thread_pool_->enqueue([this] {
        while (running_) {
            send_heartbeat();
            std::this_thread::sleep_for(std::chrono::seconds(DFS_HEARTBEAT_INTERVAL_SEC));
        }
    });
    
    return true;
}

void ChunkServer::stop() {
    running_ = false;
    if (server_socket_) {
        server_socket_->close_socket();
    }
    if (thread_pool_) {
        thread_pool_->shutdown();
    }
}

void ChunkServer::accept_connections() {
    while (running_) {
        std::string client_ip;
        int client_fd = server_socket_->accept_connection(client_ip);
        
        if (client_fd < 0) {
            continue;
        }
        
        // Handle client in thread pool
        thread_pool_->enqueue([this, client_fd, client_ip] {
            handle_client_connection(client_fd, client_ip);
        });
    }
}

void ChunkServer::handle_client_connection(int client_fd, const std::string& client_ip) {
    NetworkSocket client_socket;
    // Simulate socket assignment (in real implementation, wrap fd)
    
    std::cout << "New client connection from " << client_ip << std::endl;
    
    while (running_) {
        ProtocolFrame request;
        // In real implementation:
        // if (!client_socket.recv_frame(request)) break;
        
        ProtocolFrame response;
        response.magic = DFS_PROTOCOL_MAGIC;
        response.version = DFS_PROTOCOL_VERSION;
        
        // process_message(request, response);
        // client_socket.send_frame(response);
        
        break;  // Simplified for this example
    }
    
    close(client_fd);
}

bool ChunkServer::process_message(const ProtocolFrame& frame, ProtocolFrame& response) {
    response.magic = DFS_PROTOCOL_MAGIC;
    response.version = DFS_PROTOCOL_VERSION;
    response.message_type = OP_ACK;
    
    switch (frame.message_type) {
        case OP_READ: {
            FileReadRequest req;
            std::memcpy(&req, frame.payload, sizeof(FileReadRequest));
            FileReadResponse resp;
            if (handle_read(req, resp)) {
                std::memcpy(response.payload, &resp, sizeof(FileReadResponse));
                response.payload_size = sizeof(FileReadResponse);
            }
            break;
        }
        
        case OP_WRITE: {
            FileWriteRequest req;
            std::memcpy(&req, frame.payload, sizeof(FileWriteRequest));
            FileWriteResponse resp;
            if (handle_write(req, resp)) {
                std::memcpy(response.payload, &resp, sizeof(FileWriteResponse));
                response.payload_size = sizeof(FileWriteResponse);
            }
            break;
        }
        
        case OP_DELETE: {
            uint64_t chunk_id;
            std::memcpy(&chunk_id, frame.payload, sizeof(uint64_t));
            delete_chunk(chunk_id);
            response.message_type = OP_ACK;
            break;
        }
        
        case OP_REPLICATE: {
            handle_replicate(frame);
            break;
        }
        
        default:
            response.message_type = OP_ACK;
    }
    
    response.checksum = NetworkSocket::calculate_crc32(response.payload, response.payload_size);
    return true;
}

bool ChunkServer::handle_read(const FileReadRequest& req, FileReadResponse& resp) {
    std::unique_lock<std::mutex> lock(chunks_mutex_);
    
    auto it = chunks_.find(req.chunk_id);
    if (it == chunks_.end()) {
        resp.success = false;
        resp.error_message = "Chunk not found";
        return false;
    }
    
    StoredChunk& chunk = it->second;
    uint32_t read_size = std::min(req.length, (uint32_t)chunk.size - req.offset);
    
    if (req.offset >= chunk.size) {
        resp.success = false;
        resp.error_message = "Offset out of range";
        return false;
    }
    
    resp.data.insert(resp.data.end(), 
                    chunk.data.begin() + req.offset,
                    chunk.data.begin() + req.offset + read_size);
    resp.success = true;
    chunk.last_access = std::time(nullptr);
    
    return true;
}

bool ChunkServer::handle_write(const FileWriteRequest& req, FileWriteResponse& resp) {
    std::unique_lock<std::mutex> lock(chunks_mutex_);
    
    // Check if we have space
    if (used_capacity_ + req.data.size() > max_capacity_) {
        resp.success = false;
        resp.error_message = "Insufficient storage capacity";
        return false;
    }
    
    auto it = chunks_.find(req.chunk_id);
    if (it == chunks_.end()) {
        // Create new chunk
        StoredChunk new_chunk;
        new_chunk.chunk_id = req.chunk_id;
        new_chunk.version = 1;
        new_chunk.creation_time = std::time(nullptr);
        new_chunk.last_access = new_chunk.creation_time;
        new_chunk.data = req.data;
        new_chunk.size = req.data.size();
        new_chunk.checksum = NetworkSocket::calculate_crc32(req.data.data(), req.data.size());
        
        chunks_[req.chunk_id] = new_chunk;
        used_capacity_ += new_chunk.size;
    } else {
        // Update existing chunk
        StoredChunk& chunk = it->second;
        size_t new_size = std::max(req.offset + (uint32_t)req.data.size(), (uint32_t)chunk.size);
        
        if (used_capacity_ - chunk.size + new_size > max_capacity_) {
            resp.success = false;
            resp.error_message = "Insufficient storage capacity";
            return false;
        }
        
        used_capacity_ -= chunk.size;
        chunk.data.resize(new_size);
        std::memcpy(chunk.data.data() + req.offset, req.data.data(), req.data.size());
        chunk.size = new_size;
        chunk.version++;
        chunk.last_access = std::time(nullptr);
        chunk.checksum = NetworkSocket::calculate_crc32(chunk.data.data(), chunk.size);
        used_capacity_ += chunk.size;
    }
    
    resp.chunk_id = req.chunk_id;
    resp.success = true;
    return true;
}

bool ChunkServer::delete_chunk(uint64_t chunk_id) {
    std::unique_lock<std::mutex> lock(chunks_mutex_);
    
    auto it = chunks_.find(chunk_id);
    if (it == chunks_.end()) {
        return false;
    }
    
    used_capacity_ -= it->second.size;
    chunks_.erase(it);
    return true;
}

bool ChunkServer::write_chunk(uint64_t chunk_id, const std::vector<uint8_t>& data) {
    FileWriteRequest req;
    req.chunk_id = chunk_id;
    req.offset = 0;
    req.data = data;
    req.version = 1;
    
    FileWriteResponse resp;
    return handle_write(req, resp);
}

bool ChunkServer::read_chunk(uint64_t chunk_id, std::vector<uint8_t>& data) {
    FileReadRequest req;
    req.chunk_id = chunk_id;
    req.offset = 0;
    req.length = DFS_CHUNK_SIZE_BYTES;
    
    FileReadResponse resp;
    if (handle_read(req, resp)) {
        data = resp.data;
        return true;
    }
    return false;
}

uint64_t ChunkServer::get_available_capacity() const {
    return (max_capacity_ > used_capacity_) ? (max_capacity_ - used_capacity_) : 0;
}

ChunkServerStatus ChunkServer::get_status() const {
    ChunkServerStatus status;
    status.server_id = server_id_;
    status.ip_address = ip_;
    status.port = port_;
    status.total_capacity_bytes = max_capacity_;
    status.used_capacity_bytes = used_capacity_;
    status.is_healthy = running_;
    status.last_heartbeat = std::time(nullptr);
    
    {
        std::unique_lock<std::mutex> lock(chunks_mutex_);
        for (const auto& entry : chunks_) {
            status.healthy_chunks.push_back(entry.first);
        }
    }
    
    return status;
}

bool ChunkServer::replicate_chunk(uint64_t chunk_id, const std::string& target_ip, uint16_t target_port) {
    std::vector<uint8_t> data;
    if (!read_chunk(chunk_id, data)) {
        return false;
    }
    
    NetworkSocket target_socket;
    if (!target_socket.connect_to_server(target_ip, target_port)) {
        return false;
    }
    
    ProtocolFrame frame;
    frame.magic = DFS_PROTOCOL_MAGIC;
    frame.version = DFS_PROTOCOL_VERSION;
    frame.message_type = OP_REPLICATE;
    
    // Simplified replication (in production, handle large chunks)
    frame.payload_size = sizeof(uint64_t) + data.size();
    uint8_t* ptr = frame.payload;
    std::memcpy(ptr, &chunk_id, sizeof(uint64_t));
    std::memcpy(ptr + sizeof(uint64_t), data.data(), data.size());
    
    frame.checksum = NetworkSocket::calculate_crc32(frame.payload, frame.payload_size);
    
    return target_socket.send_frame(frame);
}

void ChunkServer::send_heartbeat() {
    if (!metadata_client_ || !metadata_client_->is_connected()) {
        metadata_client_ = std::make_unique<NetworkSocket>();
        if (!metadata_client_->connect_to_server(metadata_server_ip_, metadata_server_port_)) {
            return;
        }
    }
    
    ChunkServerStatus status = get_status();
    
    ProtocolFrame frame;
    frame.magic = DFS_PROTOCOL_MAGIC;
    frame.version = DFS_PROTOCOL_VERSION;
    frame.message_type = OP_HEARTBEAT;
    
    HeartbeatMessage msg;
    msg.server_id = status.server_id;
    msg.timestamp = status.last_heartbeat;
    msg.healthy_chunks = status.healthy_chunks;
    msg.total_capacity = status.total_capacity_bytes;
    msg.used_capacity = status.used_capacity_bytes;
    msg.replication_queue_length = status.replication_queue_length;
    
    frame.payload_size = sizeof(HeartbeatMessage);
    std::memcpy(frame.payload, &msg, frame.payload_size);
    frame.checksum = NetworkSocket::calculate_crc32(frame.payload, frame.payload_size);
    
    metadata_client_->send_frame(frame);
}
