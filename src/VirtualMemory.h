#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include "Cache.h"

using ll = long long;
using u64 = uint64_t;

const u64 PAGE_SIZE = 64;
const u64 VIRTUAL_MEM_SIZE = 4096;
const u64 PHYSICAL_MEM_SIZE = 1024;

enum PageReplacementAlgo { VM_FIFO, VM_LRU, VM_CLOCK };

// Represents a translation cached in the Translation Lookaside Buffer (TLB).
struct TLBEntry {
    bool valid = false;
    u64 vpn = 0;
    u64 pfn = 0;
    u64 last_access = 0;
};

// Set-associative Translation Lookaside Buffer with LRU replacement.
class TLB {
public:
    int sets;
    int ways;
    u64 timer = 0;
    std::vector<std::vector<TLBEntry>> table;

    // Initializes TLB with given entry count and set associativity.
    TLB(int num_entries, int assoc);

    // Checks TLB for virtual page number translation. Returns physical frame number or -1 on miss.
    int lookup(u64 vpn);

    // Inserts or updates VPN -> PFN mapping in TLB using LRU eviction.
    void insert(u64 vpn, u64 pfn);
};

// Represents a single page entry in the page table.
struct PageTableEntry {
    bool valid = false;
    bool dirty = false;
    bool referenced = false;
    int frame_number = -1;
    u64 last_access_time = 0;
    u64 loaded_time = 0;
};

// Virtual Memory Manager and Memory Management Unit (MMU) simulator.
// Handles address translation, page fault servicing, page eviction (FIFO/LRU/Clock),
// and cache invalidation on page eviction.
class VirtualMemory {
private:
    std::vector<PageTableEntry> page_table;
    std::vector<int> frame_table;
    u64 total_frames;
    u64 access_counter = 0;
    u64 page_faults = 0;
    u64 page_hits = 0;
    u64 disk_accesses = 0;
    
    PageReplacementAlgo policy;
    int clock_hand = 0;
    MemoryHierarchy* cache_ptr; 

    // Scans physical frame table for an unused frame. Returns frame index or -1 if full.
    int find_free_frame();

    // Selects a victim page based on current replacement policy and evicts it.
    int evict_page();

public:
    // Initializes MMU with physical frames, page table, and connects to cache hierarchy.
    VirtualMemory(MemoryHierarchy* cache, PageReplacementAlgo p = VM_LRU);

    // Updates the active page replacement algorithm (FIFO, LRU, CLOCK).
    void set_replacement_policy(PageReplacementAlgo p);

    // Translates virtual address to physical address via TLB/PageTable, handling page faults.
    ll translate(u64 v_addr, bool is_write, TLB& tlb, std::string& report);

    // Displays VM hits, faults, and disk I/O metrics.
    void get_statistics();
};
