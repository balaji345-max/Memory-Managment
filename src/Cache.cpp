#include "Cache.h"
#include <iostream>
#include <iomanip>
#include <cmath>
#include <limits>
#include <stdexcept>

CacheLevel::CacheLevel(int id, u64 s, u64 bs, int assoc, ReplacementPolicy p)
    : level_id(id), size(s), block_size(bs), associativity(assoc), policy(p) {

    if (s == 0 || bs == 0 || assoc == 0) throw std::invalid_argument("Params cannot be 0");
    num_sets = size / (block_size * associativity);
    if ((num_sets & (num_sets - 1)) != 0) throw std::invalid_argument("Sets must be power of 2");

    offset_bits = static_cast<u64>(std::log2(block_size));
    index_bits  = static_cast<u64>(std::log2(num_sets));
    sets.resize(num_sets, std::vector<CacheLine>(associativity));
}

void CacheLevel::set_policy(ReplacementPolicy p) { policy = p; }

bool CacheLevel::access(u64 address, bool is_write) {
    access_counter++;
    u64 index = (address >> offset_bits) % num_sets;
    u64 tag = address >> (offset_bits + index_bits);

    for (auto& line : sets[index]) {
        if (line.valid && line.tag == tag) {
            hits++;
            line.last_access_time = access_counter;
            line.freq++;
            if (is_write) line.dirty = true;
            return true;
        }
    }
    misses++;
    return false;
}

bool CacheLevel::insert(u64 address, bool is_write, u64& ev_addr, bool& ev_dirty) {
    u64 index = (address >> offset_bits) % num_sets;
    u64 tag = address >> (offset_bits + index_bits);

    int victim = -1;
    u64 min_val = std::numeric_limits<u64>::max();

    for (int i = 0; i < associativity; i++) {
        if (!sets[index][i].valid) { victim = i; break; }
        u64 val = (policy == LRU) ? sets[index][i].last_access_time : 
                  (policy == FIFO) ? sets[index][i].insertion_time : sets[index][i].freq;
        if (val < min_val) { min_val = val; victim = i; }
    }

    bool evicted = false;
    if (sets[index][victim].valid) {
        evicted = true;
        ev_addr = (sets[index][victim].tag << (offset_bits + index_bits)) | (index << offset_bits);
        ev_dirty = sets[index][victim].dirty;
    }

    sets[index][victim] = {true, is_write, tag, access_counter, access_counter, 1};
    return evicted;
}

bool CacheLevel::invalidate(u64 address) {
    u64 index = (address >> offset_bits) % num_sets;
    u64 tag = address >> (offset_bits + index_bits);
    for (auto& line : sets[index]) {
        if (line.valid && line.tag == tag) {
            bool was_dirty = line.dirty;
            line.valid = false;
            return was_dirty;
        }
    }
    return false;
}

void CacheLevel::invalidate_frame(size_t start, size_t range) {
    for (size_t a = start; a < start + range; a += block_size) invalidate(a);
}

MemoryHierarchy::MemoryHierarchy(CacheLevel* a, CacheLevel* b, CacheLevel* c) : l1(a), l2(b), l3(c) {}

void MemoryHierarchy::invalidate_physical_range(size_t addr, size_t size) {
    l1->invalidate_frame(addr, size);
    l2->invalidate_frame(addr, size);
    l3->invalidate_frame(addr, size);
}

std::string MemoryHierarchy::request(u64 address, bool is_write) {
    u64 ev_addr; bool ev_dirty;
    if (l1->access(address, is_write)) return "L1 Hit";
    
    if (l2->access(address, is_write)) {
        if (l1->insert(address, is_write, ev_addr, ev_dirty) && ev_dirty) handle_writeback(ev_addr, 1);
        return "L2 Hit";
    }
    
    if (l3->access(address, is_write)) {
        if (l2->insert(address, is_write, ev_addr, ev_dirty)) {
            if (l1->invalidate(ev_addr)) ev_dirty = true;
            if (ev_dirty) handle_writeback(ev_addr, 2);
        }
        l1->insert(address, is_write, ev_addr, ev_dirty);
        return "L3 Hit";
    }

    if (l3->insert(address, is_write, ev_addr, ev_dirty)) {
        if (l2->invalidate(ev_addr) || l1->invalidate(ev_addr)) ev_dirty = true;
        if (ev_dirty) handle_writeback(ev_addr, 3);
    }
    l2->insert(address, is_write, ev_addr, ev_dirty);
    l1->insert(address, is_write, ev_addr, ev_dirty);
    return "RAM Miss (Fetched to Caches)";
}

void MemoryHierarchy::handle_writeback(u64 addr, int level) {
    if (level == 1) l2->access(addr, true);
    else if (level == 2) l3->access(addr, true);
}
void CacheLevel::display_stats() const {
    double hr = (access_counter > 0) ? (double)hits / access_counter * 100.0 : 0.0;

    std::cout << "L" << level_id << " Stats: "
              << "Hits="   << std::setw(5) << std::left << hits 
              << " | Misses=" << std::setw(5) << std::left << misses 
              << " | Hit Rate=" << std::fixed << std::setprecision(2) << std::setw(6) << std::right << hr << "%\n";
}

void MemoryHierarchy::display_all_stats() const {
    std::cout << "\n--- Cache Hierarchy Statistics ---\n";
    l1->display_stats();
    l2->display_stats();
    l3->display_stats();
    std::cout << "----------------------------------\n";
}