// src/core/threading/decode_thread_pool.h
// Specialized thread pool for hardware decoding operations
// Each thread has its own persistent CUDA context
#pragma once

#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <functional>
#include <atomic>
#include <string>

#ifdef HAVE_CUDA
#include <cuda.h>
#endif

namespace fluxvision {
namespace threading {

// Specialized decode thread pool with CUDA context per thread
class DecodeThreadPool {
public:
    struct Config {
        size_t numThreads = 4;
        int cudaDeviceId = 0;
        bool enableWorkStealing = true;
    };

    struct Stats {
        std::vector<size_t> perThreadDecodeCount;
        size_t totalDecodes = 0;
        size_t tasksInQueue = 0;
    };

    explicit DecodeThreadPool(const Config& config);
    ~DecodeThreadPool();

    // Delete copy/move
    DecodeThreadPool(const DecodeThreadPool&) = delete;
    DecodeThreadPool& operator=(const DecodeThreadPool&) = delete;

    // Submit decode task (returns immediately)
    // Task receives CUcontext as parameter to use for decoding
#ifdef HAVE_CUDA
    void submitDecodeTask(const std::string& cameraId,
                          std::function<void(CUcontext)> task);
#else
    void submitDecodeTask(const std::string& cameraId,
                          std::function<void(void*)> task);
#endif

    // Shutdown pool
    void shutdown(bool waitForTasks = true);

    // Check if running
    bool isRunning() const { return running_.load(); }

    // Statistics
    Stats getStats() const;

private:
    struct DecodeWorker {
        std::thread thread;
#ifdef HAVE_CUDA
        CUcontext cudaContext = nullptr;
#else
        void* cudaContext = nullptr;
#endif
        std::atomic<size_t> decodesProcessed{0};
        std::atomic<bool> busy{false};
        size_t workerId = 0;
    };

    struct DecodeTask {
        std::string cameraId;
#ifdef HAVE_CUDA
        std::function<void(CUcontext)> task;
#else
        std::function<void(void*)> task;
#endif
    };

    Config config_;
    std::vector<std::unique_ptr<DecodeWorker>> workers_;
    std::atomic<bool> running_{true};

    // Work queue (shared across all decode threads for work-stealing)
    std::queue<DecodeTask> tasks_;
    mutable std::mutex queueMutex_;
    std::condition_variable condition_;

    void decodeWorkerLoop(size_t workerId);
    bool initializeCudaContext(DecodeWorker& worker);
    void cleanupCudaContext(DecodeWorker& worker);
};

} // namespace threading
} // namespace fluxvision
