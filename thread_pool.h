// ============================================================================
// DISTRIBUTED FILE SYSTEM - THREAD POOL IMPLEMENTATION
// ============================================================================
// File: thread_pool.h
// Description: Efficient thread pool for concurrent task processing
// ============================================================================

#ifndef DFS_THREAD_POOL_H
#define DFS_THREAD_POOL_H

#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <vector>
#include <memory>

class ThreadPool {
public:
    explicit ThreadPool(size_t num_threads);
    ~ThreadPool();
    
    // Enqueue a task for execution
    void enqueue(std::function<void()> task);
    
    // Stop the thread pool and wait for all tasks to complete
    void shutdown();
    
    // Get number of worker threads
    size_t get_num_threads() const { return threads_.size(); }
    
    // Get number of pending tasks
    size_t get_pending_tasks() const;

private:
    std::vector<std::thread> threads_;
    std::queue<std::function<void()>> tasks_;
    mutable std::mutex queue_mutex_;
    std::condition_variable cv_;
    bool stop_;
    
    void worker_thread();
};

#endif // DFS_THREAD_POOL_H


// ============================================================================
// File: thread_pool.cpp
// ============================================================================

#include "thread_pool.h"
#include <iostream>

ThreadPool::ThreadPool(size_t num_threads) : stop_(false) {
    for (size_t i = 0; i < num_threads; ++i) {
        threads_.emplace_back([this] { worker_thread(); });
    }
}

ThreadPool::~ThreadPool() {
    shutdown();
}

void ThreadPool::worker_thread() {
    while (true) {
        std::function<void()> task;
        
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            cv_.wait(lock, [this] { return !tasks_.empty() || stop_; });
            
            if (stop_ && tasks_.empty()) {
                return;
            }
            
            if (tasks_.empty()) {
                continue;
            }
            
            task = std::move(tasks_.front());
            tasks_.pop();
        }
        
        try {
            task();
        } catch (const std::exception& e) {
            std::cerr << "Thread pool task error: " << e.what() << std::endl;
        }
    }
}

void ThreadPool::enqueue(std::function<void()> task) {
    {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        if (stop_) {
            throw std::runtime_error("ThreadPool is stopped");
        }
        tasks_.emplace(std::move(task));
    }
    cv_.notify_one();
}

size_t ThreadPool::get_pending_tasks() const {
    std::unique_lock<std::mutex> lock(queue_mutex_);
    return tasks_.size();
}

void ThreadPool::shutdown() {
    {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        stop_ = true;
    }
    cv_.notify_all();
    
    for (auto& thread : threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
}
