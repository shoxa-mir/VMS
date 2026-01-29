// src/core/stream/pipeline.h
// Complete streaming pipeline - top-level integration
#pragma once

#include "stream_manager.h"
#include "../threading/network_thread_pool.h"
#include "../threading/decode_thread_pool.h"
#include "../gpu/memory_pool.h"
#include <memory>

namespace fluxvision {
namespace stream {

// Complete pipeline statistics
struct PipelineStats {
    threading::ThreadPool::Stats networkPoolStats;
    threading::DecodeThreadPool::Stats decodePoolStats;
    gpu::GPUMemoryPool::Stats memoryStats;
    GlobalStats streamStats;
};

// Complete streaming pipeline for 42+ cameras
class StreamPipeline {
public:
    struct Config {
        // Thread pool configuration
        size_t networkThreads = 8;      // Network receive threads
        size_t decodeThreads = 4;       // Hardware decode threads
        int cudaDeviceId = 0;           // CUDA device for decoding

        // Queue configuration
        size_t packetQueueSize = 60;    // Per-camera packet queue size (2 seconds @ 30fps)

        // GPU memory configuration
        size_t maxGpuMemoryBytes = 3ULL * 1024 * 1024 * 1024;  // 3GB limit
        bool enableMemoryWarnings = true;

        // Surface configuration (for pre-allocation)
        uint32_t defaultSurfaceWidth = 1920;
        uint32_t defaultSurfaceHeight = 1080;
    };

    explicit StreamPipeline(const Config& config);
    ~StreamPipeline();

    // Delete copy/move
    StreamPipeline(const StreamPipeline&) = delete;
    StreamPipeline& operator=(const StreamPipeline&) = delete;

    // Initialize all components
    bool initialize();

    // Shutdown all components
    void shutdown();

    // Check if initialized
    bool isInitialized() const { return initialized_; }

    // Access to stream manager (main interface)
    StreamManager* getStreamManager() { return streamManager_.get(); }

    // Access to thread pools (for advanced use)
    threading::NetworkThreadPool* getNetworkPool() { return networkPool_.get(); }
    threading::DecodeThreadPool* getDecodePool() { return decodePool_.get(); }
    gpu::GPUMemoryPool* getMemoryPool() { return memoryPool_.get(); }

    // Statistics from all components
    PipelineStats getStats() const;

    // Configuration
    const Config& getConfig() const { return config_; }

private:
    Config config_;
    std::atomic<bool> initialized_{false};

    // Core components (owned by pipeline)
    std::unique_ptr<threading::NetworkThreadPool> networkPool_;
    std::unique_ptr<threading::DecodeThreadPool> decodePool_;
    std::unique_ptr<gpu::GPUMemoryPool> memoryPool_;
    std::unique_ptr<StreamManager> streamManager_;
};

} // namespace stream
} // namespace fluxvision
