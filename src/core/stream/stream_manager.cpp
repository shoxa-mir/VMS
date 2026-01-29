// src/core/stream/stream_manager.cpp
#include "stream_manager.h"
#include <iostream>

namespace fluxvision {
namespace stream {

StreamManager::StreamManager() {
}

StreamManager::~StreamManager() {
    shutdown();
}

bool StreamManager::initialize(threading::NetworkThreadPool* networkPool,
                               threading::DecodeThreadPool* decodePool,
                               gpu::GPUMemoryPool* memoryPool) {
    if (initialized_) {
        return true;  // Already initialized
    }

    if (!networkPool || !decodePool || !memoryPool) {
        std::cerr << "StreamManager: null thread pool or memory pool provided" << std::endl;
        return false;
    }

    networkPool_ = networkPool;
    decodePool_ = decodePool;
    memoryPool_ = memoryPool;

    initialized_ = true;
    running_ = true;

    return true;
}

bool StreamManager::addCamera(const CameraStream::Config& config) {
    if (!initialized_) {
        std::cerr << "StreamManager: not initialized" << std::endl;
        return false;
    }

    // Check if camera already exists
    {
        std::shared_lock<std::shared_mutex> lock(camerasMutex_);
        if (cameras_.find(config.id) != cameras_.end()) {
            std::cerr << "StreamManager: camera " << config.id << " already exists" << std::endl;
            return false;
        }
    }

    // Create camera stream
    auto camera = std::make_unique<CameraStream>(config);

    // Start camera
    if (!camera->start()) {
        std::cerr << "StreamManager: failed to start camera " << config.id << std::endl;
        return false;
    }

    // Assign camera to network thread
    networkPool_->assignCamera(config.id);

    // Add to registry
    {
        std::unique_lock<std::shared_mutex> lock(camerasMutex_);
        cameras_[config.id] = std::move(camera);
    }

    // Start network receive loop for this camera
    startNetworkReceiveLoop(config.id);

    // Start decode loop for this camera
    startDecodeLoop(config.id);

    std::cout << "StreamManager: added camera " << config.id << std::endl;
    return true;
}

bool StreamManager::removeCamera(const std::string& id) {
    std::unique_lock<std::shared_mutex> lock(camerasMutex_);

    auto it = cameras_.find(id);
    if (it == cameras_.end()) {
        return false;  // Camera not found
    }

    // Stop camera
    it->second->stop();

    // Unassign from network thread
    networkPool_->unassignCamera(id);

    // Remove from registry
    cameras_.erase(it);

    std::cout << "StreamManager: removed camera " << id << std::endl;
    return true;
}

void StreamManager::setQuality(const std::string& id, StreamQuality quality) {
    std::shared_lock<std::shared_mutex> lock(camerasMutex_);

    auto it = cameras_.find(id);
    if (it != cameras_.end()) {
        it->second->setQuality(quality);
    }
}

CameraStream* StreamManager::getCamera(const std::string& id) {
    std::shared_lock<std::shared_mutex> lock(camerasMutex_);

    auto it = cameras_.find(id);
    if (it != cameras_.end()) {
        return it->second.get();
    }

    return nullptr;
}

void StreamManager::startAll() {
    std::shared_lock<std::shared_mutex> lock(camerasMutex_);

    for (auto& [id, camera] : cameras_) {
        if (camera->getState() == StreamState::STOPPED) {
            camera->start();
        }
    }
}

void StreamManager::stopAll() {
    std::shared_lock<std::shared_mutex> lock(camerasMutex_);

    for (auto& [id, camera] : cameras_) {
        camera->stop();
    }
}

void StreamManager::setAllQuality(StreamQuality quality) {
    std::shared_lock<std::shared_mutex> lock(camerasMutex_);

    for (auto& [id, camera] : cameras_) {
        camera->setQuality(quality);
    }
}

void StreamManager::reconnectAll() {
    std::shared_lock<std::shared_mutex> lock(camerasMutex_);

    for (auto& [id, camera] : cameras_) {
        if (camera->getState() == StreamState::ERROR) {
            camera->reconnect();
        }
    }
}

void StreamManager::setFrameCallback(FrameCallback callback) {
    std::lock_guard<std::mutex> lock(callbackMutex_);
    frameCallback_ = std::move(callback);
}

GlobalStats StreamManager::getGlobalStats() const {
    std::shared_lock<std::shared_mutex> lock(camerasMutex_);

    GlobalStats stats;
    stats.totalCameras = cameras_.size();

    double totalFps = 0.0;

    for (const auto& [id, camera] : cameras_) {
        auto cameraStats = camera->getStats();
        auto state = camera->getState();

        if (state == StreamState::RUNNING) {
            stats.activeCameras++;
            totalFps += cameraStats.currentFps;
        } else if (state == StreamState::ERROR) {
            stats.errorCameras++;
        } else if (state == StreamState::RECONNECTING) {
            stats.reconnectingCameras++;
        }

        stats.totalDroppedFrames += cameraStats.droppedFrames;
        stats.totalDecodedFrames += cameraStats.decodedFrames;
    }

    if (stats.activeCameras > 0) {
        stats.avgFps = totalFps / stats.activeCameras;
    }

    // Get memory statistics from GPU memory pool
    if (memoryPool_) {
        stats.memoryStats = memoryPool_->getStats();
    }

    return stats;
}

std::vector<std::string> StreamManager::getCameraIds() const {
    std::shared_lock<std::shared_mutex> lock(camerasMutex_);

    std::vector<std::string> ids;
    ids.reserve(cameras_.size());

    for (const auto& [id, camera] : cameras_) {
        ids.push_back(id);
    }

    return ids;
}

size_t StreamManager::getCameraCount() const {
    std::shared_lock<std::shared_mutex> lock(camerasMutex_);
    return cameras_.size();
}

void StreamManager::shutdown() {
    if (!running_) {
        return;
    }

    running_ = false;
    stopAll();

    std::unique_lock<std::shared_mutex> lock(camerasMutex_);
    cameras_.clear();

    initialized_ = false;
}

void StreamManager::startNetworkReceiveLoop(const std::string& cameraId) {
    // Submit network receive task to network thread pool
    networkPool_->submit([this, cameraId]() {
        // Get camera
        CameraStream* camera = getCamera(cameraId);
        if (!camera) {
            return;
        }

        auto* rtspClient = camera->getRtspClient();
        auto* packetQueue = camera->getPacketQueue();

        if (!rtspClient || !packetQueue) {
            return;
        }

        // Network receive loop (runs continuously while camera is active)
        while (running_ && camera->isRunning()) {
            try {
                // Receive NAL units from RTSP client
                std::vector<network::NalUnit> nalUnits;
                if (rtspClient->receiveNalUnits(nalUnits)) {
                    // Push NAL units to packet queue
                    for (auto& nal : nalUnits) {
                        StreamPacket packet;
                        packet.data = std::move(nal.data);
                        packet.timestamp = nal.pts;
                        packet.isKeyFrame = nal.isKeyframe;

                        // Push or drop oldest if queue full (backpressure)
                        packetQueue->pushOrDropOldest(std::move(packet));
                    }
                }
            }
            catch (const std::exception& e) {
                std::cerr << "Network receive error for camera " << cameraId
                          << ": " << e.what() << std::endl;

                // Attempt reconnection if enabled
                if (camera->getConfig().autoReconnect) {
                    camera->reconnect();
                }
                break;
            }
        }
    });
}

void StreamManager::startDecodeLoop(const std::string& cameraId) {
    // Submit decode task to decode thread pool
    decodePool_->submitDecodeTask(cameraId, [this, cameraId](CUcontext /* cudaContext */) {
        // Get camera (CUcontext is set current by decode thread, decoder uses it implicitly)
        CameraStream* camera = getCamera(cameraId);
        if (!camera) {
            return;
        }

        auto* decoder = camera->getDecoder();
        auto* packetQueue = camera->getPacketQueue();

        if (!decoder || !packetQueue) {
            return;
        }

        // Decode loop (runs continuously while camera is active)
        while (running_ && camera->isRunning()) {
            StreamPacket packet;

            // Try to pop packet from queue
            if (packetQueue->pop(packet)) {
                try {
                    // Decode packet
                    DecodeResult result = decoder->decode(packet.data.data(), packet.data.size());

                    if (result.status == DecodeStatus::SUCCESS && result.frame) {
                        // Notify frame callback
                        onFrameDecoded(cameraId, result.frame);
                    }
                }
                catch (const std::exception& e) {
                    std::cerr << "Decode error for camera " << cameraId
                              << ": " << e.what() << std::endl;
                }
            } else {
                // Queue empty, brief sleep to avoid busy-wait
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
    });
}

void StreamManager::onFrameDecoded(const std::string& cameraId,
                                   const DecodedFrame* frame) {
    // Invoke user callback if set
    std::lock_guard<std::mutex> lock(callbackMutex_);
    if (frameCallback_) {
        frameCallback_(cameraId, frame);
    }
}

} // namespace stream
} // namespace fluxvision
