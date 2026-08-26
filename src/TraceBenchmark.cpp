#include "TraceBenchmark.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <chrono>
#include <random>
#include <algorithm>
#include <cmath>

// ==================== load_trace ==============================

bool TraceBenchmark::load_trace(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cout << "[Bench] Error: cannot open trace file '" << filename << "'\n";
        return false;
    }

    trace.clear();
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream iss(line);
        std::string op_str;
        iss >> op_str;

        TraceEntry entry{};
        if (op_str == "R" || op_str == "r") {
            entry.op = TraceEntry::READ;
            iss >> std::hex >> entry.address;
        } else if (op_str == "W" || op_str == "w") {
            entry.op = TraceEntry::WRITE;
            iss >> std::hex >> entry.address;
        } else if (op_str == "A" || op_str == "a") {
            entry.op = TraceEntry::ALLOC;
            iss >> std::dec >> entry.size;
        } else if (op_str == "F" || op_str == "f") {
            entry.op = TraceEntry::FREE;
            iss >> std::dec >> entry.block_id;
        } else {
            continue;  // skip unknown lines
        }
        trace.push_back(entry);
    }

    std::cout << "[Bench] Loaded " << trace.size() << " entries from '" << filename << "'\n";
    return true;
}

// ==================== generate_trace ==========================

void TraceBenchmark::generate_trace(const std::string& pattern, size_t count, u64 addr_range) {
    trace.clear();
    trace.reserve(count);

    std::mt19937_64 rng(42);  // deterministic seed for reproducibility
    std::uniform_int_distribution<u64> addr_dist(0, addr_range - 1);
    std::uniform_real_distribution<double> coin(0.0, 1.0);

    if (pattern == "sequential") {
        for (size_t i = 0; i < count; ++i) {
            u64 addr = (i * PAGE_SIZE) % addr_range;
            trace.push_back({coin(rng) < 0.8 ? TraceEntry::READ : TraceEntry::WRITE, addr, 0, 0});
        }
    }
    else if (pattern == "random") {
        for (size_t i = 0; i < count; ++i) {
            u64 addr = (addr_dist(rng) / PAGE_SIZE) * PAGE_SIZE;
            trace.push_back({coin(rng) < 0.7 ? TraceEntry::READ : TraceEntry::WRITE, addr, 0, 0});
        }
    }
    else if (pattern == "temporal") {
        // 80% of accesses to 20% of addresses (hot/cold locality)
        u64 hot_range = std::max<u64>(addr_range / 5, PAGE_SIZE);
        std::uniform_int_distribution<u64> hot_dist(0, hot_range - 1);
        for (size_t i = 0; i < count; ++i) {
            u64 addr;
            if (coin(rng) < 0.8) {
                addr = (hot_dist(rng) / PAGE_SIZE) * PAGE_SIZE;
            } else {
                addr = (addr_dist(rng) / PAGE_SIZE) * PAGE_SIZE;
            }
            trace.push_back({coin(rng) < 0.75 ? TraceEntry::READ : TraceEntry::WRITE, addr, 0, 0});
        }
    }
    else if (pattern == "strided") {
        u64 stride = PAGE_SIZE * 4;  // 4-page stride
        u64 addr = 0;
        for (size_t i = 0; i < count; ++i) {
            trace.push_back({coin(rng) < 0.8 ? TraceEntry::READ : TraceEntry::WRITE, addr % addr_range, 0, 0});
            addr += stride;
        }
    }
    else if (pattern == "spec_int") {
        // Models integer workload: 70% read, moderate locality, some allocations
        u64 hot_range = addr_range / 4;
        std::uniform_int_distribution<u64> hot_dist(0, hot_range - 1);
        for (size_t i = 0; i < count; ++i) {
            double r = coin(rng);
            if (r < 0.03) {
                // 3% allocs
                std::uniform_int_distribution<size_t> sz_dist(8, 256);
                trace.push_back({TraceEntry::ALLOC, 0, sz_dist(rng), 0});
            } else if (r < 0.04) {
                // 1% frees
                if (!trace.empty()) {
                    std::uniform_int_distribution<int> id_dist(1, std::max(1, static_cast<int>(i)));
                    trace.push_back({TraceEntry::FREE, 0, 0, id_dist(rng)});
                }
            } else {
                u64 addr = (coin(rng) < 0.6) 
                    ? (hot_dist(rng) / PAGE_SIZE) * PAGE_SIZE 
                    : (addr_dist(rng) / PAGE_SIZE) * PAGE_SIZE;
                trace.push_back({r < 0.74 ? TraceEntry::READ : TraceEntry::WRITE, addr, 0, 0});
            }
        }
    }
    else if (pattern == "spec_fp") {
        // Models FP workload: streaming access with periodic reuse
        u64 stream_pos = 0;
        u64 reuse_base = 0;
        u64 reuse_range = std::min<u64>(addr_range / 8, PAGE_SIZE * 8);
        for (size_t i = 0; i < count; ++i) {
            double r = coin(rng);
            u64 addr;
            if (r < 0.4) {
                // 40%: streaming sequential
                addr = stream_pos % addr_range;
                stream_pos += PAGE_SIZE;
            } else if (r < 0.8) {
                // 40%: reuse (working set)
                std::uniform_int_distribution<u64> reuse_dist(0, reuse_range - 1);
                addr = (reuse_base + reuse_dist(rng)) % addr_range;
                addr = (addr / PAGE_SIZE) * PAGE_SIZE;
            } else {
                // 20%: random
                addr = (addr_dist(rng) / PAGE_SIZE) * PAGE_SIZE;
            }
            trace.push_back({coin(rng) < 0.6 ? TraceEntry::READ : TraceEntry::WRITE, addr, 0, 0});
            // Periodically shift the reuse window
            if (i % 200 == 0) {
                reuse_base = (reuse_base + reuse_range) % addr_range;
            }
        }
    }
    else {
        std::cout << "[Bench] Unknown pattern '" << pattern << "'. Use: sequential, random, temporal, strided, spec_int, spec_fp\n";
        return;
    }

    std::cout << "[Bench] Generated " << trace.size() << " entries (pattern=" << pattern << ", range=" << addr_range << ")\n";
}

// ==================== run =====================================

BenchmarkResult TraceBenchmark::run(Allocator* alloc, MemoryHierarchy& cache,
                                     VirtualMemory& mmu, TLB& tlb,
                                     Alloc_Algo algo) {
    result = {};
    has_result = true;

    // Snapshot stats before
    u64 l1_hits_0 = cache.get_l1()->get_hits();
    u64 l1_miss_0 = cache.get_l1()->get_misses();
    u64 l2_hits_0 = cache.get_l2()->get_hits();
    u64 l2_miss_0 = cache.get_l2()->get_misses();
    u64 l3_hits_0 = cache.get_l3()->get_hits();
    u64 l3_miss_0 = cache.get_l3()->get_misses();
    u64 vm_hits_0 = mmu.get_page_hits();
    u64 vm_faults_0 = mmu.get_page_faults();

    auto t_start = std::chrono::high_resolution_clock::now();

    for (auto& entry : trace) {
        result.total_ops++;
        std::string report;
        switch (entry.op) {
            case TraceEntry::READ:
            case TraceEntry::WRITE: {
                result.mem_ops++;
                ll p_addr = mmu.translate(entry.address, entry.op == TraceEntry::WRITE, tlb, report);
                if (p_addr != -1) {
                    cache.request(static_cast<u64>(p_addr), entry.op == TraceEntry::WRITE);
                }
                break;
            }
            case TraceEntry::ALLOC: {
                result.alloc_count++;
                int id = alloc->allocate(entry.size, algo);
                if (id == -1) result.alloc_failures++;
                break;
            }
            case TraceEntry::FREE: {
                result.free_count++;
                alloc->deallocate(entry.block_id);
                break;
            }
        }
    }

    auto t_end = std::chrono::high_resolution_clock::now();
    double elapsed_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();

    // Snapshot stats after
    u64 l1_hits  = cache.get_l1()->get_hits()   - l1_hits_0;
    u64 l1_miss  = cache.get_l1()->get_misses() - l1_miss_0;
    u64 l2_hits  = cache.get_l2()->get_hits()   - l2_hits_0;
    u64 l2_miss  = cache.get_l2()->get_misses() - l2_miss_0;
    u64 l3_hits  = cache.get_l3()->get_hits()   - l3_hits_0;
    u64 l3_miss  = cache.get_l3()->get_misses() - l3_miss_0;
    u64 vm_hits  = mmu.get_page_hits()   - vm_hits_0;
    u64 vm_faults = mmu.get_page_faults() - vm_faults_0;

    u64 total_mem = result.mem_ops;
    u64 l1_acc = l1_hits + l1_miss;
    u64 l2_acc = l2_hits + l2_miss;
    u64 l3_acc = l3_hits + l3_miss;
    u64 vm_acc = vm_hits + vm_faults;

    result.l1_hit_rate  = l1_acc > 0 ? 100.0 * l1_hits / l1_acc : 0.0;
    result.l2_hit_rate  = l2_acc > 0 ? 100.0 * l2_hits / l2_acc : 0.0;
    result.l3_hit_rate  = l3_acc > 0 ? 100.0 * l3_hits / l3_acc : 0.0;
    result.tlb_hit_rate = vm_acc > 0 ? 100.0 * vm_hits / vm_acc : 0.0;
    result.page_fault_rate = vm_acc > 0 ? 100.0 * vm_faults / vm_acc : 0.0;

    // Compute AMAT
    double l1_mr = l1_acc > 0 ? (double)l1_miss / l1_acc : 0.0;
    double l2_mr = l2_acc > 0 ? (double)l2_miss / l2_acc : 0.0;
    double l3_mr = l3_acc > 0 ? (double)l3_miss / l3_acc : 0.0;
    double pf_r  = vm_acc > 0 ? (double)vm_faults / vm_acc : 0.0;
    double tlb_mr = vm_acc > 0 ? 1.0 - ((double)vm_hits / vm_acc) : 0.0;

    // AMAT = TLB_time + L1_time + L1_miss_rate * (L2_time + L2_miss_rate * (L3_time + L3_miss_rate * RAM_time))
    //        + TLB_miss_rate * walk_cost + page_fault_rate * disk_cost
    result.amat_ns = latency.tlb_hit_ns + latency.l1_hit_ns
                   + l1_mr * (latency.l2_hit_ns
                   + l2_mr * (latency.l3_hit_ns
                   + l3_mr * latency.ram_ns))
                   + tlb_mr * (latency.walk_per_level_ns * 4.0)
                   + pf_r * latency.disk_ns;

    result.wall_time_ms = elapsed_ms;
    result.throughput_ops = elapsed_ms > 0 ? (result.total_ops / (elapsed_ms / 1000.0)) : 0.0;

    return result;
}

// ==================== report ==================================

void TraceBenchmark::report() const {
    if (!has_result) {
        std::cout << "[Bench] No benchmark results available. Run a benchmark first.\n";
        return;
    }

    std::cout << "\n";
    std::cout << "╔══════════════════════════════════════════════════╗\n";
    std::cout << "║          SPEC CPU Trace Benchmark Report        ║\n";
    std::cout << "╠══════════════════════════════════════════════════╣\n";

    std::cout << "║ Total operations    : " << std::setw(26) << result.total_ops     << " ║\n";
    std::cout << "║ Memory ops (R/W)    : " << std::setw(26) << result.mem_ops       << " ║\n";
    std::cout << "║ Allocations         : " << std::setw(26) << result.alloc_count   << " ║\n";
    std::cout << "║ Frees               : " << std::setw(26) << result.free_count    << " ║\n";
    std::cout << "║ Alloc failures      : " << std::setw(26) << result.alloc_failures<< " ║\n";
    std::cout << "╠══════════════════════════════════════════════════╣\n";

    auto pct = [](double v) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2) << v << "%";
        return oss.str();
    };
    auto ns = [](double v) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2) << v << " ns";
        return oss.str();
    };

    std::cout << "║ TLB hit rate        : " << std::setw(26) << pct(result.tlb_hit_rate)    << " ║\n";
    std::cout << "║ L1 hit rate         : " << std::setw(26) << pct(result.l1_hit_rate)     << " ║\n";
    std::cout << "║ L2 hit rate         : " << std::setw(26) << pct(result.l2_hit_rate)     << " ║\n";
    std::cout << "║ L3 hit rate         : " << std::setw(26) << pct(result.l3_hit_rate)     << " ║\n";
    std::cout << "║ Page fault rate     : " << std::setw(26) << pct(result.page_fault_rate) << " ║\n";
    std::cout << "╠══════════════════════════════════════════════════╣\n";

    std::cout << "║ AMAT (avg)          : " << std::setw(26) << ns(result.amat_ns)          << " ║\n";

    std::ostringstream wall_str;
    wall_str << std::fixed << std::setprecision(3) << result.wall_time_ms << " ms";
    std::cout << "║ Wall-clock time     : " << std::setw(26) << wall_str.str()               << " ║\n";

    std::ostringstream tp_str;
    tp_str << std::fixed << std::setprecision(0) << result.throughput_ops << " ops/s";
    std::cout << "║ Throughput          : " << std::setw(26) << tp_str.str()                 << " ║\n";

    std::cout << "╚══════════════════════════════════════════════════╝\n";
}
