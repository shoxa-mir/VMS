// src/core/stream/stream_manager.h
// Multi-camera coordinator for 42+ simultaneous streams
#pragma once

#include "camera_stream.h"
#include "../threading/network_thread_pool.h"
#include "../threading/decode_thread_pool.h"
#include "../gpu/memory_pool.h"
#include "../codec/types.h"
#include <unordered_map>
#include <shared_mutex>
#include <functional>
#include <vector>

namespace fluxvision {
namespace stream {

// Frame callback signature (called when frame is decoded)
using FrameCallback = std::function<void(const std::string& cameraId,
                                         const DecodedFrame* frame)>;

// Global statistics across all cameras
struct GlobalStats {
    size_t totalCameras = 0;
    size_t activeCameras = 0;
    size_t errorCameras = 0;
    size_t reconnectingCameras = 0;
    double avgFps = 0.0;
    size_t totalDroppedFrames = 0;
    size_t totalDecodedFrames = 0;
    gpu::GPUMemoryPool::Stats memoryStats;
};

// Manages multiple camera streams
class StreamManager {
public:
    explicit StreamManager();
    ~StreamManager();

    // Delete copy/move
    StreamManager(const StreamManager&) = delete;
    StreamManager& operator=(const StreamManager&) = delete;

    // Initialize with thread pools and memory pool
    bool initialize(threading::NetworkThreadPool* networkPool,
                   threading::DecodeThreadPool* decodePool,
                   gpu::GPUMemoryPool* memoryPool);

    // Camera management
    bool addCamera(const CameraStream::Config& config);
    bool removeCamera(const std::string& id);
    void setQuality(const std::string& id, StreamQuality quality);
    CameraStream* getCamera(const std::string& id);

    // Batch operations
    void startAll();
    void stopAll();
    void setAllQuality(StreamQuality quality);
    void reconnectAll();

    // Frame callback
    void setFrameCallback(FrameCallback callback);

    // Statistics
    GlobalStats getGlobalStats() const;
    std::vector<std::string> getCameraIds() const;
    size_t getCameraCount() const;

    // Lifecycle
    bool isInitialized() const { return initialized_; }
    void shutdown();

private:
    // Thread pools (not owned, injected)
    threading::NetworkThreadPool* networkPool_ = nullptr;
    threading::DecodeThreadPool* decodePool_ = nullptr;
    gpu::GPUMemoryPool* memoryPool_ = nullptr;

    // Camera registry
    std::unordered_map<std::string, std::unique_ptr<CameraStream>> cameras_;
    mutable std::shared_mutex camerasMutex_;  // Read-write lock for camera access

    // Frame callback
    FrameCallback frameCallback_;
    std::mutex callbackMutex_;

    // Lifecycle
    std::atomic<bool> initialized_{false};
    std::atomic<bool> running_{false};

    // Internal helpers
    void startNetworkReceiveLoop(const std::string& cameraId);
    void startDecodeLoop(const std::string& cameraId);
    void onFrameDecoded(const std::string& cameraId, const DecodedFrame* frame);
};

} // namespace stream
} // namespace fluxvision
