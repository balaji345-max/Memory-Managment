#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include "Allocator.h"
#include "Cache.h"
#include "VirtualMemory.h"

using u64 = uint64_t;

struct TraceEntry {
    enum Op { READ, WRITE, ALLOC, FREE } op;
    u64 address;
    size_t size;   // used for ALLOC
    int block_id;  // used for FREE
};

struct LatencyConfig {
    double tlb_hit_ns       = 0.5;
    double l1_hit_ns        = 1.0;
    double l2_hit_ns        = 5.0;
    double l3_hit_ns        = 20.0;
    double ram_ns           = 100.0;
    double walk_per_level_ns = 10.0;
    double disk_ns          = 10000.0;
};

struct BenchmarkResult {
    double amat_ns         = 0.0;
    double throughput_ops  = 0.0;
    double wall_time_ms    = 0.0;
    u64    total_ops       = 0;
    u64    mem_ops          = 0;

    double tlb_hit_rate    = 0.0;
    double l1_hit_rate     = 0.0;
    double l2_hit_rate     = 0.0;
    double l3_hit_rate     = 0.0;
    double page_fault_rate = 0.0;
    double avg_walk_cost   = 0.0;

    u64 alloc_count = 0;
    u64 free_count  = 0;
    u64 alloc_failures = 0;
};

// Trace-driven benchmarking engine.  Reads/generates memory-access traces
// and executes them through the full allocator → MMU → cache pipeline,
// collecting AMAT, throughput, and per-level hit-rate statistics.
class TraceBenchmark {
private:
    std::vector<TraceEntry> trace;
    LatencyConfig latency;
    BenchmarkResult result;
    bool has_result = false;

public:
    TraceBenchmark() = default;

    // Load trace from a file (format: "R 0xADDR" / "W 0xADDR" / "A SIZE" / "F ID")
    bool load_trace(const std::string& filename);

    // Generate synthetic trace with the given pattern and count.
    // Patterns: "sequential", "random", "temporal", "strided", "spec_int", "spec_fp"
    void generate_trace(const std::string& pattern, size_t count, u64 addr_range = 4096);

    // Execute the loaded trace through the full pipeline.
    BenchmarkResult run(Allocator* alloc, MemoryHierarchy& cache,
                        VirtualMemory& mmu, TLB& tlb,
                        Alloc_Algo algo = Firstfit);

    // Print a formatted report of the last benchmark run.
    void report() const;

    // Accessors
    size_t trace_size() const { return trace.size(); }
    const LatencyConfig& get_latency() const { return latency; }
    void set_latency(const LatencyConfig& lc) { latency = lc; }
};
