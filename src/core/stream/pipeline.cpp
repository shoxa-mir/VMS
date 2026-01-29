// src/core/stream/pipeline.cpp
#include "pipeline.h"
#include <iostream>

namespace fluxvision {
namespace stream {

StreamPipeline::StreamPipeline(const Config& config)
    : config_(config)
{
}

StreamPipeline::~StreamPipeline() {
    shutdown();
}

bool StreamPipeline::initialize() {
    if (initialized_) {
        std::cout << "StreamPipeline: already initialized" << std::endl;
        return true;
    }

    std::cout << "StreamPipeline: initializing..." << std::endl;

    // 1. Initialize GPU Memory Pool
    {
        gpu::GPUMemoryPool::Config memConfig;
        memConfig.maxGpuMemoryBytes = config_.maxGpuMemoryBytes;
        memConfig.enableWarnings = config_.enableMemoryWarnings;

        memoryPool_ = std::make_unique<gpu::GPUMemoryPool>(memConfig);

        if (!memoryPool_->initialize()) {
            std::cerr << "StreamPipeline: failed to initialize GPU memory pool" << std::endl;
            return false;
        }

        std::cout << "StreamPipeline: GPU memory pool initialized (limit: "
                  << (config_.maxGpuMemoryBytes / (1024 * 1024)) << " MB)" << std::endl;
    }

    // 2. Initialize Network Thread Pool
    {
        networkPool_ = std::make_unique<threading::NetworkThreadPool>(config_.networkThreads);

        std::cout << "StreamPipeline: network thread pool initialized ("
                  << config_.networkThreads << " threads)" << std::endl;
    }

    // 3. Initialize Decode Thread Pool
    {
        threading::DecodeThreadPool::Config decodeConfig;
        decodeConfig.numThreads = config_.decodeThreads;
        decodeConfig.cudaDeviceId = config_.cudaDeviceId;
        decodeConfig.enableWorkStealing = true;

        decodePool_ = std::make_unique<threading::DecodeThreadPool>(decodeConfig);

        std::cout << "StreamPipeline: decode thread pool initialized ("
                  << config_.decodeThreads << " threads, CUDA device "
                  << config_.cudaDeviceId << ")" << std::endl;
    }

    // 4. Initialize Stream Manager
    {
        streamManager_ = std::make_unique<StreamManager>();

        if (!streamManager_->initialize(networkPool_.get(),
                                       decodePool_.get(),
                                       memoryPool_.get())) {
            std::cerr << "StreamPipeline: failed to initialize stream manager" << std::endl;
            return false;
        }

        std::cout << "StreamPipeline: stream manager initialized" << std::endl;
    }

    initialized_ = true;

    std::cout << "StreamPipeline: initialization complete" << std::endl;
    std::cout << "  - Network threads: " << config_.networkThreads << std::endl;
    std::cout << "  - Decode threads: " << config_.decodeThreads << std::endl;
    std::cout << "  - GPU memory limit: " << (config_.maxGpuMemoryBytes / (1024 * 1024)) << " MB" << std::endl;
    std::cout << "  - Packet queue size: " << config_.packetQueueSize << std::endl;

    return true;
}

void StreamPipeline::shutdown() {
    if (!initialized_) {
        return;
    }

    std::cout << "StreamPipeline: shutting down..." << std::endl;

    // Shutdown in reverse order of initialization
    if (streamManager_) {
        streamManager_->shutdown();
        streamManager_.reset();
    }

    if (decodePool_) {
        decodePool_->shutdown(true);  // Wait for pending tasks
        decodePool_.reset();
    }

    if (networkPool_) {
        networkPool_->shutdown(true);  // Wait for pending tasks
        networkPool_.reset();
    }

    if (memoryPool_) {
        memoryPool_.reset();
    }

    initialized_ = false;

    std::cout << "StreamPipeline: shutdown complete" << std::endl;
}

PipelineStats StreamPipeline::getStats() const {
    PipelineStats stats;

    if (networkPool_) {
        stats.networkPoolStats = networkPool_->getStats();
    }

    if (decodePool_) {
        stats.decodePoolStats = decodePool_->getStats();
    }

    if (memoryPool_) {
        stats.memoryStats = memoryPool_->getStats();
    }

    if (streamManager_) {
        stats.streamStats = streamManager_->getGlobalStats();
    }

    return stats;
}

} // namespace stream
} // namespace fluxvision
