#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <memory>
#include "ThreadSafety.h"

using u64 = uint64_t;

enum ReplacementPolicy { LRU, FIFO, LFU };

// Represents a single cache line in a set-associative cache.
struct CacheLine {
    bool valid = false;
    bool dirty = false;
    u64 tag = 0;
    u64 last_access_time = 0;
    u64 insertion_time = 0;
    u64 freq = 0;
};

// Represents one level (e.g. L1, L2, L3) of set-associative CPU cache.
// Thread-safe: per-set SpinLock allows concurrent access to different sets.
class CacheLevel {
private:
    int level_id;
    u64 size;              
    u64 block_size;         
    int associativity;
    u64 num_sets;
    u64 offset_bits;
    u64 index_bits;
    ReplacementPolicy policy;
    std::vector<std::vector<CacheLine>> sets;
    u64 hits = 0;
    u64 misses = 0;
    u64 access_counter = 0;

    // Per-set spinlocks for fine-grained concurrency
    SpinLock* set_locks = nullptr;
    
public:
    // Initializes cache level parameters and calculates indexing bits.
    CacheLevel(int id, u64 s, u64 bs, int assoc, ReplacementPolicy p);

    ~CacheLevel();

    // Non-copyable, non-movable (owns raw spinlock array)
    CacheLevel(const CacheLevel&) = delete;
    CacheLevel& operator=(const CacheLevel&) = delete;

    // Updates cache line eviction policy (LRU, FIFO, LFU).
    void set_policy(ReplacementPolicy p);

    // Probes cache for an address. Returns true on hit and updates access statistics.
    bool access(u64 address, bool is_write);

    // Invalidates matching cache line. Returns true if line was dirty (needs writeback).
    bool invalidate(u64 address);

    // Invalidates all cache lines spanning the specified memory range.
    void invalidate_frame(size_t start, size_t range);

    // Inserts a new line into the appropriate set, choosing a victim based on replacement policy.
    bool insert(u64 address, bool is_write, u64& evicted_addr, bool& evicted_dirty);

    // Prints hits, misses, and hit rate percentage for this cache level.
    void display_stats() const;

    // Resets all statistics counters (for benchmarking before/after snapshots).
    void reset_stats();

    // Accessors for benchmarking and visualization
    u64 get_hits()          const { return hits; }
    u64 get_misses()        const { return misses; }
    u64 get_accesses()      const { return access_counter; }
    int get_level_id()      const { return level_id; }
    u64 get_num_sets()      const { return num_sets; }
    int get_associativity() const { return associativity; }
    u64 get_block_size()    const { return block_size; }
    u64 get_size()          const { return size; }
    const std::vector<std::vector<CacheLine>>& get_sets() const { return sets; }
};

// Multi-level cache hierarchy controller (L1 -> L2 -> L3 -> RAM).
// Coordinates cache lookups, line allocation, and writeback propagation across levels.
class MemoryHierarchy {
private:
    CacheLevel* l1;
    CacheLevel* l2;
    CacheLevel* l3;

    // Propagates evicted dirty lines down to lower cache levels.
    void handle_writeback(u64 address, int from_level);
    
public:
    // Connects L1, L2, and L3 cache instances into a hierarchy.
    MemoryHierarchy(CacheLevel* _l1, CacheLevel* _l2, CacheLevel* _l3);

    // Simulates a memory access through L1, L2, L3 hierarchy, returning access outcome description.
    std::string request(u64 address, bool is_write);

    // Flushes/invalidates a physical address range across all cache levels.
    void invalidate_physical_range(size_t addr, size_t size);

    // Prints comprehensive statistics for all cache levels.
    void display_all_stats() const;

    // Resets statistics across all levels.
    void reset_all_stats();

    // Level accessors for benchmarking
    CacheLevel* get_l1() { return l1; }
    CacheLevel* get_l2() { return l2; }
    CacheLevel* get_l3() { return l3; }
};