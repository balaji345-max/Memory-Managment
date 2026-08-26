#pragma once
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include <string>
#include <functional>
#include "Allocator.h"
#include "Cache.h"
#include "VirtualMemory.h"

// Per-thread statistics collected during concurrent execution.
struct ThreadStats {
    int thread_id = 0;
    u64 allocs     = 0;
    u64 frees      = 0;
    u64 reads      = 0;
    u64 writes     = 0;
    u64 alloc_fails = 0;
    u64 total_ops  = 0;
    double elapsed_ms = 0.0;
};

// Aggregated results across all threads.
struct ThreadedResult {
    int num_threads = 0;
    u64 total_ops   = 0;
    double wall_time_ms = 0.0;
    double throughput_ops_sec = 0.0;
    double avg_per_thread_ms  = 0.0;

    u64 total_allocs = 0;
    u64 total_frees  = 0;
    u64 total_reads  = 0;
    u64 total_writes = 0;
    u64 total_alloc_fails = 0;

    bool race_detected = false;
    std::vector<ThreadStats> per_thread;
};

// Multithreaded stress-test engine.
// Spawns N worker threads that concurrently perform malloc/free/read/write
// operations through the full allocator → MMU → cache pipeline, validating
// that all locking primitives hold under contention.
class ThreadedBenchmark {
private:
    ThreadedResult result;
    bool has_result = false;

    // Atomic counter used for cross-thread allocation ID tracking
    std::atomic<int> shared_alloc_counter{0};

public:
    ThreadedBenchmark() = default;

    // Run a concurrent workload with the given number of threads.
    // Each thread performs ops_per_thread operations (mixed alloc/free/read/write).
    // The workload mix is: 20% alloc, 10% free, 40% read, 30% write.
    ThreadedResult run(int num_threads, int ops_per_thread,
                       Allocator* alloc, MemoryHierarchy& cache,
                       VirtualMemory& mmu, TLB& tlb,
                       Alloc_Algo algo = Firstfit);

    // Run allocator-only stress test (malloc/free only, no MMU/cache).
    ThreadedResult run_alloc_stress(int num_threads, int ops_per_thread,
                                    Allocator* alloc, Alloc_Algo algo = Firstfit);

    // Run cache-only stress test (read/write only through cache hierarchy).
    ThreadedResult run_cache_stress(int num_threads, int ops_per_thread,
                                    MemoryHierarchy& cache, u64 addr_range = 4096);

    // Print formatted report.
    void report() const;
};
