// src/core/threading/decode_thread_pool.cpp
#include "decode_thread_pool.h"
#include "../gpu/cuda_context.h"
#include <iostream>

namespace fluxvision {
namespace threading {

DecodeThreadPool::DecodeThreadPool(const Config& config)
    : config_(config)
{
    workers_.reserve(config_.numThreads);

    for (size_t i = 0; i < config_.numThreads; ++i) {
        auto worker = std::make_unique<DecodeWorker>();
        worker->workerId = i;
        worker->thread = std::thread([this, i]() { decodeWorkerLoop(i); });

        workers_.push_back(std::move(worker));
    }
}

DecodeThreadPool::~DecodeThreadPool() {
    shutdown(true);
}

void DecodeThreadPool::shutdown(bool waitForTasks) {
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        running_ = false;

        if (!waitForTasks) {
            // Clear pending tasks
            std::queue<DecodeTask> empty;
            std::swap(tasks_, empty);
        }
    }

    condition_.notify_all();

    // Join all threads
    for (auto& worker : workers_) {
        if (worker->thread.joinable()) {
            worker->thread.join();
        }
    }

    // Cleanup CUDA contexts
    for (auto& worker : workers_) {
        cleanupCudaContext(*worker);
    }
}

#ifdef HAVE_CUDA
void DecodeThreadPool::submitDecodeTask(const std::string& cameraId,
                                        std::function<void(CUcontext)> task)
#else
void DecodeThreadPool::submitDecodeTask(const std::string& cameraId,
                                        std::function<void(void*)> task)
#endif
{
    {
        std::lock_guard<std::mutex> lock(queueMutex_);

        if (!running_) {
            throw std::runtime_error("DecodeThreadPool: Cannot submit task to stopped pool");
        }

        tasks_.push({cameraId, std::move(task)});
    }

    condition_.notify_one();
}

void DecodeThreadPool::decodeWorkerLoop(size_t workerId) {
    DecodeWorker& worker = *workers_[workerId];

    // Initialize CUDA context for this thread
    if (!initializeCudaContext(worker)) {
        std::cerr << "DecodeThreadPool: Worker " << workerId
                  << " failed to initialize CUDA context" << std::endl;
        return;
    }

    while (true) {
        DecodeTask task;

        {
            std::unique_lock<std::mutex> lock(queueMutex_);

            condition_.wait(lock, [this] {
                return !running_ || !tasks_.empty();
            });

            if (!running_ && tasks_.empty()) {
                break;
            }

            if (!tasks_.empty()) {
                task = std::move(tasks_.front());
                tasks_.pop();
                worker.busy = true;
            }
        }

        if (task.task) {
            // Execute task with this thread's CUDA context
            task.task(worker.cudaContext);
            worker.decodesProcessed++;
            worker.busy = false;
        }
    }
}

bool DecodeThreadPool::initializeCudaContext(DecodeWorker& worker) {
#ifdef HAVE_CUDA
    // Create dedicated CUDA context for this decode thread
    worker.cudaContext = CudaContext::createContext(config_.cudaDeviceId);

    if (!worker.cudaContext) {
        std::cerr << "DecodeThreadPool: Failed to create CUDA context for worker "
                  << worker.workerId << std::endl;
        return false;
    }

    // Make context current for this thread
    CUresult result = cuCtxSetCurrent(worker.cudaContext);
    if (result != CUDA_SUCCESS) {
        const char* errorStr = nullptr;
        cuGetErrorString(result, &errorStr);
        std::cerr << "DecodeThreadPool: Failed to set CUDA context current: "
                  << (errorStr ? errorStr : "Unknown error") << std::endl;
        CudaContext::destroyContext(worker.cudaContext);
        worker.cudaContext = nullptr;
        return false;
    }

    return true;
#else
    // No CUDA support
    worker.cudaContext = nullptr;
    return true;
#endif
}

void DecodeThreadPool::cleanupCudaContext(DecodeWorker& worker) {
#ifdef HAVE_CUDA
    if (worker.cudaContext) {
        CudaContext::destroyContext(worker.cudaContext);
        worker.cudaContext = nullptr;
    }
#endif
}

DecodeThreadPool::Stats DecodeThreadPool::getStats() const {
    Stats stats;

    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        stats.tasksInQueue = tasks_.size();
    }

    stats.perThreadDecodeCount.reserve(workers_.size());
    for (const auto& worker : workers_) {
        size_t count = worker->decodesProcessed.load();
        stats.perThreadDecodeCount.push_back(count);
        stats.totalDecodes += count;
    }

    return stats;
}

} // namespace threading
} // namespace fluxvision
