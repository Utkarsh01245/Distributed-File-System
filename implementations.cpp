// ============================================================================
// DISTRIBUTED FILE SYSTEM - METADATA SERVER IMPLEMENTATION
// ============================================================================
// File: metadata_server.h & metadata_server.cpp (Simplified)
// ============================================================================

#ifndef DFS_METADATA_SERVER_H
#define DFS_METADATA_SERVER_H

#include "common.h"
#include "network.h"
#include "thread_pool.h"
#include <string>
#include <map>
#include <mutex>
#include <memory>

class MetadataServer {
public:
    explicit MetadataServer(const std::string& ip, uint16_t port);
    ~MetadataServer();
    
    // Server lifecycle
    bool start();
    void stop();
    bool is_running() const { return running_; }
    
    // File operations
    bool create_file(const std::string& path, uint32_t permissions, uint64_t& file_id);
    bool delete_file(const std::string& path);
    bool mkdir(const std::string& path);
    bool get_file_metadata(const std::string& path, FileMetadata& metadata);
    
    // Chunk management
    bool allocate_chunks(uint64_t file_id, uint32_t num_chunks);
    std::vector<ChunkHandle> get_file_chunks(uint64_t file_id);
    
    // Chunk server heartbeat handling
    void process_heartbeat(const HeartbeatMessage& msg);
    
private:
    struct FileEntry {
        FileMetadata metadata;
        std::vector<ChunkHandle> chunks;
    };
    
    std::string ip_;
    uint16_t port_;
    bool running_;
    
    std::map<std::string, FileEntry> file_system_;
    std::map<std::string, ChunkServerStatus> chunk_servers_;
    
    std::mutex fs_mutex_;
    std::mutex servers_mutex_;
    
    std::unique_ptr<NetworkSocket> server_socket_;
    std::unique_ptr<ThreadPool> thread_pool_;
    
    // Internal methods
    void accept_connections();
    void handle_client_connection(int client_fd, const std::string& client_ip);
    bool process_message(const ProtocolFrame& frame, ProtocolFrame& response);
    std::vector<ChunkLocation> select_chunk_replicas(uint64_t chunk_id);
    uint64_t allocate_chunk_id();
};

#endif // DFS_METADATA_SERVER_H


// ============================================================================
// File: main_chunk_server.cpp - Example Chunk Server Initialization
// ============================================================================

#include "chunk_server.h"
#include <iostream>
#include <csignal>

static ChunkServer* g_chunk_server = nullptr;

void signal_handler(int signal) {
    if (g_chunk_server) {
        std::cout << "\\nShutting down chunk server..." << std::endl;
        g_chunk_server->stop();
    }
    exit(0);
}

int main(int argc, char* argv[]) {
    std::string server_id = "CS_001";
    std::string ip = "127.0.0.1";
    uint16_t port = 9001;
    std::string storage_path = "/tmp/dfs_storage_cs1";
    uint64_t max_capacity = 1024 * 1024 * 1024;  // 1 GB
    
    // Parse command line arguments if provided
    if (argc >= 2) server_id = argv[1];
    if (argc >= 3) ip = argv[2];
    if (argc >= 4) port = std::atoi(argv[3]);
    
    g_chunk_server = new ChunkServer(server_id, ip, port, storage_path, max_capacity);
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    std::cout << "========================================" << std::endl;
    std::cout << "  DISTRIBUTED FILE SYSTEM - CHUNK SERVER" << std::endl;
    std::cout << "  Server ID: " << server_id << std::endl;
    std::cout << "  Address: " << ip << ":" << port << std::endl;
    std::cout << "  Max Capacity: " << (max_capacity / 1024 / 1024) << " MB" << std::endl;
    std::cout << "========================================" << std::endl;
    
    if (!g_chunk_server->start()) {
        std::cerr << "Failed to start chunk server" << std::endl;
        delete g_chunk_server;
        return 1;
    }
    
    // Keep server running
    while (g_chunk_server->is_running()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    
    delete g_chunk_server;
    return 0;
}


// ============================================================================
// File: main_client_example.cpp - Example Client Usage
// ============================================================================

#include "client_lib.h"
#include <iostream>
#include <string>

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "  DFS CLIENT - EXAMPLE USAGE" << std::endl;
    std::cout << "========================================\\n" << std::endl;
    
    // Create client
    DistributedFileSystem dfs("127.0.0.1", 9000);
    
    // Example 1: Create a file
    std::cout << "[1] Creating file '/data/document.txt'..." << std::endl;
    if (dfs.create_file("/data/document.txt", 0644) == 0) {
        std::cout << "✓ File created successfully\\n" << std::endl;
    } else {
        std::cerr << "✗ Failed to create file\\n" << std::endl;
    }
    
    // Example 2: Open and write file
    std::cout << "[2] Opening file for writing..." << std::endl;
    int fd = dfs.open("/data/document.txt", 0x01);  // Write mode
    if (fd >= 0) {
        const char* data = "Hello, Distributed File System!";
        size_t written = dfs.write(fd, data, strlen(data));
        std::cout << "✓ Written " << written << " bytes\\n" << std::endl;
        dfs.close(fd);
    }
    
    // Example 3: Open and read file
    std::cout << "[3] Opening file for reading..." << std::endl;
    fd = dfs.open("/data/document.txt", 0x00);  // Read mode
    if (fd >= 0) {
        char buffer[256] = {0};
        size_t read_bytes = dfs.read(fd, buffer, sizeof(buffer) - 1);
        std::cout << "✓ Read " << read_bytes << " bytes: " << buffer << "\\n" << std::endl;
        dfs.close(fd);
    }
    
    // Example 4: Get file metadata
    std::cout << "[4] Getting file metadata..." << std::endl;
    FileMetadata metadata;
    if (dfs.get_file_info("/data/document.txt", metadata)) {
        std::cout << "✓ File ID: " << metadata.file_id << std::endl;
        std::cout << "  File Size: " << metadata.file_size << " bytes" << std::endl;
        std::cout << "  Replication Factor: " << metadata.replication_factor << "\\n" << std::endl;
    }
    
    // Example 5: Create directory
    std::cout << "[5] Creating directory '/archive'..." << std::endl;
    if (dfs.mkdir("/archive") == 0) {
        std::cout << "✓ Directory created successfully\\n" << std::endl;
    }
    
    // Example 6: Delete file
    std::cout << "[6] Deleting file '/data/document.txt'..." << std::endl;
    if (dfs.delete_file("/data/document.txt") == 0) {
        std::cout << "✓ File deleted successfully\\n" << std::endl;
    }
    
    std::cout << "========================================" << std::endl;
    std::cout << "  Example completed" << std::endl;
    std::cout << "========================================" << std::endl;
    
    return 0;
}


// ============================================================================
// File: CMakeLists.txt - Build Configuration
// ============================================================================

cmake_minimum_required(VERSION 3.10)
project(DistributedFileSystem CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -Wall -Wextra")

# Find required packages
find_package(Threads REQUIRED)
find_package(SQLite3 REQUIRED)

# Header files
set(HEADERS
    common.h
    thread_pool.h
    network.h
    client_lib.h
    chunk_server.h
)

# Source files
set(SOURCES
    thread_pool.h
    network.h
    client_lib.h
    chunk_server.h
)

# Include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

# Chunk Server executable
add_executable(chunk_server
    main_chunk_server.cpp
    ${SOURCES}
)
target_link_libraries(chunk_server
    Threads::Threads
    ${SQLITE3_LIBRARIES}
)

# Client example executable
add_executable(dfs_client
    main_client_example.cpp
    ${SOURCES}
)
target_link_libraries(dfs_client
    Threads::Threads
    ${SQLITE3_LIBRARIES}
)

# Compile options
if(UNIX)
    target_compile_options(chunk_server PRIVATE -Wl,--no-undefined)
    target_compile_options(dfs_client PRIVATE -Wl,--no-undefined)
endif()

# Installation
install(TARGETS chunk_server dfs_client DESTINATION bin)
install(FILES ${HEADERS} DESTINATION include/dfs)


// ============================================================================
// File: Makefile - Alternative Build System
// ============================================================================

CXX = g++
CXXFLAGS = -std=c++17 -O3 -Wall -Wextra -pthread
LDFLAGS = -lsqlite3 -lpthread

SOURCES = thread_pool.h network.h client_lib.h chunk_server.h
HEADERS = common.h thread_pool.h network.h client_lib.h chunk_server.h

# Targets
CHUNK_SERVER = chunk_server
CLIENT_EXAMPLE = dfs_client

all: $(CHUNK_SERVER) $(CLIENT_EXAMPLE)

$(CHUNK_SERVER): main_chunk_server.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS)

$(CLIENT_EXAMPLE): main_client_example.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f $(CHUNK_SERVER) $(CLIENT_EXAMPLE) *.o

run_chunk_server: $(CHUNK_SERVER)
	./$(CHUNK_SERVER) CS_001 127.0.0.1 9001

run_client: $(CLIENT_EXAMPLE)
	./$(CLIENT_EXAMPLE)

.PHONY: all clean run_chunk_server run_client
