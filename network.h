// ============================================================================
// DISTRIBUTED FILE SYSTEM - NETWORK LAYER
// ============================================================================
// File: network.h
// Description: TCP/IP socket and network utilities
// ============================================================================

#ifndef DFS_NETWORK_H
#define DFS_NETWORK_H

#include "common.h"
#include <string>
#include <vector>
#include <memory>
#include <sys/socket.h>
#include <netinet/in.h>

class NetworkSocket {
public:
    NetworkSocket();
    ~NetworkSocket();
    
    // Server-side operations
    bool create_server_socket(const std::string& ip, uint16_t port);
    bool listen_for_connections(int backlog = 5);
    int accept_connection(std::string& client_ip);
    
    // Client-side operations
    bool connect_to_server(const std::string& server_ip, uint16_t server_port);
    
    // Data transmission
    bool send_data(const std::vector<uint8_t>& data);
    bool recv_data(std::vector<uint8_t>& data, size_t max_size);
    bool send_frame(const ProtocolFrame& frame);
    bool recv_frame(ProtocolFrame& frame);
    
    // Socket management
    void close_socket();
    bool is_connected() const { return socket_fd_ != -1; }
    int get_socket_fd() const { return socket_fd_; }
    
    // Utility functions
    static uint32_t calculate_crc32(const uint8_t* data, size_t length);
    static std::string get_local_ip();

private:
    int socket_fd_;
    struct sockaddr_in local_addr_;
    struct sockaddr_in peer_addr_;
    bool is_server_;
    
    bool set_socket_options();
};

// Connection pool for efficient client connections
class ConnectionPool {
public:
    explicit ConnectionPool(size_t pool_size = 10);
    ~ConnectionPool();
    
    std::shared_ptr<NetworkSocket> acquire(const std::string& server_ip, uint16_t port);
    void release(const std::string& key, std::shared_ptr<NetworkSocket> socket);
    void clear();

private:
    struct PooledConnection {
        std::shared_ptr<NetworkSocket> socket;
        time_t last_used;
    };
    
    std::map<std::string, std::vector<PooledConnection>> pools_;
    std::mutex pool_mutex_;
    size_t max_pool_size_;
    
    std::string make_key(const std::string& ip, uint16_t port);
};

#endif // DFS_NETWORK_H


// ============================================================================
// File: network.cpp
// ============================================================================

#include "network.h"
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <iostream>
#include <ifaddrs.h>

// CRC32 lookup table for fast computation
static uint32_t crc32_table[256];

static void init_crc32_table() {
    static bool initialized = false;
    if (initialized) return;
    
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t crc = i;
        for (uint32_t j = 0; j < 8; ++j) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xEDB88320;
            } else {
                crc >>= 1;
            }
        }
        crc32_table[i] = crc;
    }
    initialized = true;
}

uint32_t NetworkSocket::calculate_crc32(const uint8_t* data, size_t length) {
    init_crc32_table();
    uint32_t crc = 0xFFFFFFFF;
    
    for (size_t i = 0; i < length; ++i) {
        uint8_t byte = data[i];
        crc = (crc >> 8) ^ crc32_table[(crc ^ byte) & 0xFF];
    }
    
    return crc ^ 0xFFFFFFFF;
}

std::string NetworkSocket::get_local_ip() {
    struct ifaddrs* ifaddr = nullptr;
    std::string local_ip = "127.0.0.1";
    
    if (getifaddrs(&ifaddr) == -1) {
        return local_ip;
    }
    
    for (struct ifaddrs* ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == nullptr) continue;
        
        if (ifa->ifa_addr->sa_family == AF_INET) {
            struct sockaddr_in* sin = (struct sockaddr_in*)ifa->ifa_addr;
            std::string ip = inet_ntoa(sin->sin_addr);
            
            // Prefer non-loopback interface
            if (ip != "127.0.0.1" && std::string(ifa->ifa_name).find("eth") != std::string::npos) {
                local_ip = ip;
                break;
            }
        }
    }
    
    freeifaddrs(ifaddr);
    return local_ip;
}

NetworkSocket::NetworkSocket() : socket_fd_(-1), is_server_(false) {
    std::memset(&local_addr_, 0, sizeof(local_addr_));
    std::memset(&peer_addr_, 0, sizeof(peer_addr_));
}

NetworkSocket::~NetworkSocket() {
    close_socket();
}

bool NetworkSocket::set_socket_options() {
    int optval = 1;
    if (setsockopt(socket_fd_, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0) {
        return false;
    }
    
    struct timeval tv;
    tv.tv_sec = DFS_NETWORK_TIMEOUT_MS / 1000;
    tv.tv_usec = (DFS_NETWORK_TIMEOUT_MS % 1000) * 1000;
    
    if (setsockopt(socket_fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        return false;
    }
    if (setsockopt(socket_fd_, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) < 0) {
        return false;
    }
    
    return true;
}

bool NetworkSocket::create_server_socket(const std::string& ip, uint16_t port) {
    socket_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd_ < 0) {
        return false;
    }
    
    local_addr_.sin_family = AF_INET;
    local_addr_.sin_port = htons(port);
    local_addr_.sin_addr.s_addr = inet_addr(ip.c_str());
    
    if (!set_socket_options()) {
        close_socket();
        return false;
    }
    
    if (bind(socket_fd_, (struct sockaddr*)&local_addr_, sizeof(local_addr_)) < 0) {
        close_socket();
        return false;
    }
    
    is_server_ = true;
    return true;
}

bool NetworkSocket::listen_for_connections(int backlog) {
    if (listen(socket_fd_, backlog) < 0) {
        return false;
    }
    return true;
}

int NetworkSocket::accept_connection(std::string& client_ip) {
    socklen_t addr_len = sizeof(peer_addr_);
    int client_fd = accept(socket_fd_, (struct sockaddr*)&peer_addr_, &addr_len);
    
    if (client_fd >= 0) {
        client_ip = inet_ntoa(peer_addr_.sin_addr);
    }
    
    return client_fd;
}

bool NetworkSocket::connect_to_server(const std::string& server_ip, uint16_t server_port) {
    socket_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd_ < 0) {
        return false;
    }
    
    if (!set_socket_options()) {
        close_socket();
        return false;
    }
    
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    server_addr.sin_addr.s_addr = inet_addr(server_ip.c_str());
    
    if (connect(socket_fd_, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        close_socket();
        return false;
    }
    
    return true;
}

bool NetworkSocket::send_data(const std::vector<uint8_t>& data) {
    if (socket_fd_ < 0) return false;
    
    size_t total_sent = 0;
    while (total_sent < data.size()) {
        ssize_t sent = send(socket_fd_, &data[total_sent], data.size() - total_sent, 0);
        if (sent < 0) {
            return false;
        }
        total_sent += sent;
    }
    
    return true;
}

bool NetworkSocket::recv_data(std::vector<uint8_t>& data, size_t max_size) {
    if (socket_fd_ < 0) return false;
    
    data.resize(max_size);
    ssize_t received = recv(socket_fd_, data.data(), max_size, 0);
    
    if (received < 0) {
        return false;
    }
    
    data.resize(received);
    return true;
}

bool NetworkSocket::send_frame(const ProtocolFrame& frame) {
    std::vector<uint8_t> buffer;
    
    // Serialize frame header
    uint8_t* ptr = (uint8_t*)&frame;
    size_t frame_size = sizeof(uint32_t) * 4 + sizeof(uint16_t) * 2 + frame.payload_size;
    
    buffer.insert(buffer.end(), ptr, ptr + frame_size);
    
    return send_data(buffer);
}

bool NetworkSocket::recv_frame(ProtocolFrame& frame) {
    std::vector<uint8_t> header_data;
    
    // Receive header (magic + version + type + size + checksum)
    if (!recv_data(header_data, 16)) {
        return false;
    }
    
    if (header_data.size() != 16) {
        return false;
    }
    
    std::memcpy(&frame, header_data.data(), 16);
    
    // Verify magic number
    if (frame.magic != DFS_PROTOCOL_MAGIC) {
        return false;
    }
    
    // Receive payload
    std::vector<uint8_t> payload_data;
    if (!recv_data(payload_data, frame.payload_size)) {
        return false;
    }
    
    std::memcpy(frame.payload, payload_data.data(), frame.payload_size);
    
    // Verify checksum
    uint32_t calculated_crc = calculate_crc32(frame.payload, frame.payload_size);
    if (calculated_crc != frame.checksum) {
        return false;
    }
    
    return true;
}

void NetworkSocket::close_socket() {
    if (socket_fd_ >= 0) {
        close(socket_fd_);
        socket_fd_ = -1;
    }
}

// Connection Pool Implementation
ConnectionPool::ConnectionPool(size_t pool_size) : max_pool_size_(pool_size) {}

ConnectionPool::~ConnectionPool() {
    clear();
}

std::string ConnectionPool::make_key(const std::string& ip, uint16_t port) {
    return ip + ":" + std::to_string(port);
}

std::shared_ptr<NetworkSocket> ConnectionPool::acquire(const std::string& server_ip, uint16_t port) {
    std::string key = make_key(server_ip, port);
    
    std::unique_lock<std::mutex> lock(pool_mutex_);
    
    auto it = pools_.find(key);
    if (it != pools_.end() && !it->second.empty()) {
        auto conn = it->second.back();
        it->second.pop_back();
        return conn.socket;
    }
    
    // Create new connection
    auto socket = std::make_shared<NetworkSocket>();
    if (socket->connect_to_server(server_ip, port)) {
        return socket;
    }
    
    return nullptr;
}

void ConnectionPool::release(const std::string& key, std::shared_ptr<NetworkSocket> socket) {
    std::unique_lock<std::mutex> lock(pool_mutex_);
    
    if (pools_[key].size() < max_pool_size_) {
        pools_[key].push_back({socket, std::time(nullptr)});
    }
}

void ConnectionPool::clear() {
    std::unique_lock<std::mutex> lock(pool_mutex_);
    pools_.clear();
}
