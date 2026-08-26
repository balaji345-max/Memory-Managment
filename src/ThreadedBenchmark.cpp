#include "ThreadedBenchmark.h"
#include <iostream>
#include <iomanip>
#include <random>
#include <algorithm>
#include <mutex>
#include <sstream>

// ==================== Full pipeline stress test ===============

ThreadedResult ThreadedBenchmark::run(int num_threads, int ops_per_thread,
                                       Allocator* alloc, MemoryHierarchy& cache,
                                       VirtualMemory& mmu, TLB& tlb,
                                       Alloc_Algo algo) {
    result = {};
    result.num_threads = num_threads;
    result.per_thread.resize(num_threads);
    has_result = true;
    shared_alloc_counter = 0;

    // Each thread maintains its own list of allocated block IDs
    // to avoid freeing another thread's blocks (which is valid but chaotic)
    std::vector<std::thread> threads;
    std::mutex output_mutex;  // for coordinated console output

    auto t_start = std::chrono::high_resolution_clock::now();

    for (int t = 0; t < num_threads; t++) {
        threads.emplace_back([&, t, ops_per_thread, algo]() {
            ThreadStats stats;
            stats.thread_id = t;

            // Per-thread RNG with unique seed
            std::mt19937_64 rng(42 + t * 1000);
            std::uniform_real_distribution<double> coin(0.0, 1.0);
            std::uniform_int_distribution<u64> addr_dist(0, VIRTUAL_MEM_SIZE - 1);
            std::uniform_int_distribution<size_t> size_dist(8, 256);

            std::vector<int> my_blocks;  // block IDs this thread owns

            auto thread_start = std::chrono::high_resolution_clock::now();

            for (int i = 0; i < ops_per_thread; i++) {
                double r = coin(rng);

                if (r < 0.20) {
                    // 20%: malloc
                    size_t sz = size_dist(rng);
                    int id = alloc->allocate(sz, algo);
                    stats.allocs++;
                    if (id != -1) {
                        my_blocks.push_back(id);
                    } else {
                        stats.alloc_fails++;
                    }
                }
                else if (r < 0.30) {
                    // 10%: free (from own blocks only)
                    if (!my_blocks.empty()) {
                        std::uniform_int_distribution<size_t> idx_dist(0, my_blocks.size() - 1);
                        size_t idx = idx_dist(rng);
                        alloc->deallocate(my_blocks[idx]);
                        my_blocks.erase(my_blocks.begin() + idx);
                        stats.frees++;
                    }
                }
                else if (r < 0.70) {
                    // 40%: read through MMU + cache
                    u64 addr = (addr_dist(rng) / PAGE_SIZE) * PAGE_SIZE;
                    std::string report;
                    ll p_addr = mmu.translate(addr, false, tlb, report);
                    if (p_addr != -1) {
                        cache.request(static_cast<u64>(p_addr), false);
                    }
                    stats.reads++;
                }
                else {
                    // 30%: write through MMU + cache
                    u64 addr = (addr_dist(rng) / PAGE_SIZE) * PAGE_SIZE;
                    std::string report;
                    ll p_addr = mmu.translate(addr, true, tlb, report);
                    if (p_addr != -1) {
                        cache.request(static_cast<u64>(p_addr), true);
                    }
                    stats.writes++;
                }
                stats.total_ops++;
            }

            // Clean up: free remaining blocks
            for (int id : my_blocks) {
                alloc->deallocate(id);
                stats.frees++;
            }

            auto thread_end = std::chrono::high_resolution_clock::now();
            stats.elapsed_ms = std::chrono::duration<double, std::milli>(thread_end - thread_start).count();

            result.per_thread[t] = stats;
        });
    }

    // Wait for all threads to finish
    for (auto& th : threads) {
        th.join();
    }

    auto t_end = std::chrono::high_resolution_clock::now();
    result.wall_time_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();

    // Aggregate
    for (auto& s : result.per_thread) {
        result.total_ops += s.total_ops;
        result.total_allocs += s.allocs;
        result.total_frees += s.frees;
        result.total_reads += s.reads;
        result.total_writes += s.writes;
        result.total_alloc_fails += s.alloc_fails;
        result.avg_per_thread_ms += s.elapsed_ms;
    }
    result.avg_per_thread_ms /= num_threads;
    result.throughput_ops_sec = result.wall_time_ms > 0
        ? (result.total_ops / (result.wall_time_ms / 1000.0)) : 0.0;

    return result;
}

// ==================== Allocator-only stress test ==============

ThreadedResult ThreadedBenchmark::run_alloc_stress(int num_threads, int ops_per_thread,
                                                     Allocator* alloc, Alloc_Algo algo) {
    result = {};
    result.num_threads = num_threads;
    result.per_thread.resize(num_threads);
    has_result = true;

    std::vector<std::thread> threads;

    auto t_start = std::chrono::high_resolution_clock::now();

    for (int t = 0; t < num_threads; t++) {
        threads.emplace_back([&, t, ops_per_thread, algo]() {
            ThreadStats stats;
            stats.thread_id = t;

            std::mt19937_64 rng(123 + t * 777);
            std::uniform_real_distribution<double> coin(0.0, 1.0);
            std::uniform_int_distribution<size_t> size_dist(8, 128);

            std::vector<int> my_blocks;

            auto thr_start = std::chrono::high_resolution_clock::now();

            for (int i = 0; i < ops_per_thread; i++) {
                if (coin(rng) < 0.6 || my_blocks.empty()) {
                    // 60%: allocate
                    int id = alloc->allocate(size_dist(rng), algo);
                    stats.allocs++;
                    if (id != -1) my_blocks.push_back(id);
                    else stats.alloc_fails++;
                } else {
                    // 40%: free
                    std::uniform_int_distribution<size_t> idx_dist(0, my_blocks.size() - 1);
                    size_t idx = idx_dist(rng);
                    alloc->deallocate(my_blocks[idx]);
                    my_blocks.erase(my_blocks.begin() + idx);
                    stats.frees++;
                }
                stats.total_ops++;
            }

            // Cleanup
            for (int id : my_blocks) {
                alloc->deallocate(id);
                stats.frees++;
            }

            auto thr_end = std::chrono::high_resolution_clock::now();
            stats.elapsed_ms = std::chrono::duration<double, std::milli>(thr_end - thr_start).count();
            result.per_thread[t] = stats;
        });
    }

    for (auto& th : threads) th.join();

    auto t_end = std::chrono::high_resolution_clock::now();
    result.wall_time_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();

    for (auto& s : result.per_thread) {
        result.total_ops += s.total_ops;
        result.total_allocs += s.allocs;
        result.total_frees += s.frees;
        result.total_alloc_fails += s.alloc_fails;
        result.avg_per_thread_ms += s.elapsed_ms;
    }
    result.avg_per_thread_ms /= num_threads;
    result.throughput_ops_sec = result.wall_time_ms > 0
        ? (result.total_ops / (result.wall_time_ms / 1000.0)) : 0.0;

    return result;
}

// ==================== Cache-only stress test ==================

ThreadedResult ThreadedBenchmark::run_cache_stress(int num_threads, int ops_per_thread,
                                                    MemoryHierarchy& cache, u64 addr_range) {
    result = {};
    result.num_threads = num_threads;
    result.per_thread.resize(num_threads);
    has_result = true;

    std::vector<std::thread> threads;

    auto t_start = std::chrono::high_resolution_clock::now();

    for (int t = 0; t < num_threads; t++) {
        threads.emplace_back([&, t, ops_per_thread, addr_range]() {
            ThreadStats stats;
            stats.thread_id = t;

            std::mt19937_64 rng(999 + t * 333);
            std::uniform_real_distribution<double> coin(0.0, 1.0);
            std::uniform_int_distribution<u64> addr_dist(0, addr_range - 1);

            auto thr_start = std::chrono::high_resolution_clock::now();

            for (int i = 0; i < ops_per_thread; i++) {
                u64 addr = addr_dist(rng);
                bool is_write = coin(rng) < 0.3;
                cache.request(addr, is_write);
                if (is_write) stats.writes++;
                else stats.reads++;
                stats.total_ops++;
            }

            auto thr_end = std::chrono::high_resolution_clock::now();
            stats.elapsed_ms = std::chrono::duration<double, std::milli>(thr_end - thr_start).count();
            result.per_thread[t] = stats;
        });
    }

    for (auto& th : threads) th.join();

    auto t_end = std::chrono::high_resolution_clock::now();
    result.wall_time_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();

    for (auto& s : result.per_thread) {
        result.total_ops += s.total_ops;
        result.total_reads += s.reads;
        result.total_writes += s.writes;
        result.avg_per_thread_ms += s.elapsed_ms;
    }
    result.avg_per_thread_ms /= num_threads;
    result.throughput_ops_sec = result.wall_time_ms > 0
        ? (result.total_ops / (result.wall_time_ms / 1000.0)) : 0.0;

    return result;
}

// ==================== Report ==================================

void ThreadedBenchmark::report() const {
    if (!has_result) {
        std::cout << "[ThreadBench] No results available.\n";
        return;
    }

    std::cout << "\n";
    std::cout << "╔══════════════════════════════════════════════════════╗\n";
    std::cout << "║        Multithreaded Stress Test Report             ║\n";
    std::cout << "╠══════════════════════════════════════════════════════╣\n";
    std::cout << "║ Threads             : " << std::setw(30) << result.num_threads          << " ║\n";
    std::cout << "║ Total operations    : " << std::setw(30) << result.total_ops            << " ║\n";
    std::cout << "║ Allocations         : " << std::setw(30) << result.total_allocs         << " ║\n";
    std::cout << "║ Frees               : " << std::setw(30) << result.total_frees          << " ║\n";
    std::cout << "║ Reads               : " << std::setw(30) << result.total_reads          << " ║\n";
    std::cout << "║ Writes              : " << std::setw(30) << result.total_writes         << " ║\n";
    std::cout << "║ Alloc failures      : " << std::setw(30) << result.total_alloc_fails    << " ║\n";
    std::cout << "╠══════════════════════════════════════════════════════╣\n";

    std::ostringstream wall_str;
    wall_str << std::fixed << std::setprecision(3) << result.wall_time_ms << " ms";
    std::cout << "║ Wall-clock time     : " << std::setw(30) << wall_str.str()               << " ║\n";

    std::ostringstream avg_str;
    avg_str << std::fixed << std::setprecision(3) << result.avg_per_thread_ms << " ms";
    std::cout << "║ Avg per-thread time : " << std::setw(30) << avg_str.str()                << " ║\n";

    std::ostringstream tp_str;
    tp_str << std::fixed << std::setprecision(0) << result.throughput_ops_sec << " ops/s";
    std::cout << "║ Throughput          : " << std::setw(30) << tp_str.str()                 << " ║\n";

    std::string status = result.race_detected ? "RACE DETECTED!" : "CLEAN (no crashes)";
    std::cout << "║ Concurrency status  : " << std::setw(30) << status                       << " ║\n";
    std::cout << "╠══════════════════════════════════════════════════════╣\n";

    // Per-thread breakdown
    std::cout << "║  Thread  │   Ops   │ Alloc │ Free  │  Time(ms)      ║\n";
    std::cout << "║──────────┼─────────┼───────┼───────┼────────────────║\n";
    for (auto& s : result.per_thread) {
        std::ostringstream row;
        row << "║  T" << std::setw(5) << std::left << s.thread_id
            << "  │ " << std::setw(7) << std::right << s.total_ops
            << " │ " << std::setw(5) << s.allocs
            << " │ " << std::setw(5) << s.frees
            << " │ " << std::setw(13) << std::fixed << std::setprecision(3) << s.elapsed_ms << "  ║";
        std::cout << row.str() << "\n";
    }

    std::cout << "╚══════════════════════════════════════════════════════╝\n\n";
}
