#pragma once
#include <atomic>
#include <mutex>
#include <shared_mutex>
#include <thread>

// Lightweight spinlock using atomic_flag for fast-path critical sections
// (e.g., per-set cache locking, TLB access). Falls back to yield after
// a spin threshold to avoid wasting CPU on contended locks.
class SpinLock {
    std::atomic_flag flag_ = ATOMIC_FLAG_INIT;
    static constexpr int SPIN_LIMIT = 64;

public:
    void lock() {
        int spins = 0;
        while (flag_.test_and_set(std::memory_order_acquire)) {
            if (++spins > SPIN_LIMIT) {
                std::this_thread::yield();
                spins = 0;
            }
        }
    }

    void unlock() {
        flag_.clear(std::memory_order_release);
    }

    bool try_lock() {
        return !flag_.test_and_set(std::memory_order_acquire);
    }
};

// RAII guard for SpinLock (mirrors std::lock_guard interface)
class SpinLockGuard {
    SpinLock& lock_;
public:
    explicit SpinLockGuard(SpinLock& lock) : lock_(lock) { lock_.lock(); }
    ~SpinLockGuard() { lock_.unlock(); }
    SpinLockGuard(const SpinLockGuard&) = delete;
    SpinLockGuard& operator=(const SpinLockGuard&) = delete;
};
