#pragma once
#include <cstdint>
#include <vector>
#include <string>

typedef uint64_t u64;
enum ReplacementPolicy { LRU, FIFO, LFU };

struct CacheLine {
    bool valid = false;
    bool dirty = false;
    u64 tag = 0;
    u64 last_access_time = 0;
    u64 insertion_time = 0;
    u64 freq = 0;
};

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
    u64 hits = 0, misses = 0, access_counter = 0;
    
public:
    CacheLevel(int id, u64 s, u64 bs, int assoc, ReplacementPolicy p);
    void set_policy(ReplacementPolicy p);
    bool access(u64 address, bool is_write);
    bool invalidate(u64 address);
    void invalidate_frame(size_t start, size_t range);
    bool insert(u64 address, bool is_write, u64& evicted_addr, bool& evicted_dirty);
    void display_stats() const;
};

class MemoryHierarchy {
private:
    CacheLevel* l1;
    CacheLevel* l2;
    CacheLevel* l3;
    void handle_writeback(u64 address, int from_level);
    
public:
    MemoryHierarchy(CacheLevel* _l1, CacheLevel* _l2, CacheLevel* _l3);
    std::string request(u64 address, bool is_write);
    void invalidate_physical_range(size_t addr, size_t size);
    void display_all_stats() const;
};