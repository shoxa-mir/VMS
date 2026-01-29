// src/core/threading/bounded_queue.h
// Lock-free bounded SPSC (Single-Producer, Single-Consumer) queue
// Optimized for high-throughput, low-latency inter-thread communication
#pragma once

#include <vector>
#include <atomic>
#include <cstddef>

namespace fluxvision {
namespace threading {

// Lock-free bounded SPSC queue
// Producer and consumer must be on separate threads
template<typename T>
class BoundedQueue {
public:
    explicit BoundedQueue(size_t capacity = 60)
        : capacity_(capacity)
        , buffer_(capacity)
        , head_(0)
        , tail_(0)
    {
        // Capacity must be power of 2 for efficient modulo
        // Round up to next power of 2 if needed
        if ((capacity & (capacity - 1)) != 0) {
            size_t pow2 = 1;
            while (pow2 < capacity) {
                pow2 <<= 1;
            }
            capacity_ = pow2;
            buffer_.resize(capacity_);
        }
    }

    // Push item (producer side)
    // Returns: true if pushed, false if full
    bool push(T&& item) {
        const size_t currentTail = tail_.load(std::memory_order_relaxed);
        const size_t nextTail = (currentTail + 1) & (capacity_ - 1);  // Modulo using bitmask

        if (nextTail == head_.load(std::memory_order_acquire)) {
            return false;  // Queue full
        }

        buffer_[currentTail] = std::move(item);
        tail_.store(nextTail, std::memory_order_release);
        return true;
    }

    // Push with overflow strategy (drop oldest)
    // Always succeeds by making space if needed
    void pushOrDropOldest(T&& item) {
        if (!push(std::forward<T>(item))) {
            // Queue full - drop oldest, retry
            T dummy;
            pop(dummy);
            push(std::forward<T>(item));
        }
    }

    // Pop item (consumer side)
    // Returns: true if popped, false if empty
    bool pop(T& item) {
        const size_t currentHead = head_.load(std::memory_order_relaxed);

        if (currentHead == tail_.load(std::memory_order_acquire)) {
            return false;  // Queue empty
        }

        item = std::move(buffer_[currentHead]);
        head_.store((currentHead + 1) & (capacity_ - 1), std::memory_order_release);
        return true;
    }

    // Check if empty (approximate, lock-free)
    bool empty() const {
        return head_.load(std::memory_order_acquire) ==
               tail_.load(std::memory_order_acquire);
    }

    // Get current size (approximate)
    size_t size() const {
        const size_t h = head_.load(std::memory_order_acquire);
        const size_t t = tail_.load(std::memory_order_acquire);

        if (t >= h) {
            return t - h;
        } else {
            return capacity_ - h + t;
        }
    }

    // Get capacity
    size_t capacity() const { return capacity_; }

    // Check if full (approximate)
    bool full() const {
        const size_t currentTail = tail_.load(std::memory_order_relaxed);
        const size_t nextTail = (currentTail + 1) & (capacity_ - 1);
        return nextTail == head_.load(std::memory_order_acquire);
    }

private:
    size_t capacity_;
    std::vector<T> buffer_;

    // Cache-line padding to prevent false sharing (intentional alignment)
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4324)  // Structure padded due to alignment specifier
#endif
    alignas(64) std::atomic<size_t> head_;  // Consumer index
    alignas(64) std::atomic<size_t> tail_;  // Producer index
#ifdef _MSC_VER
#pragma warning(pop)
#endif
};

} // namespace threading
} // namespace fluxvision
