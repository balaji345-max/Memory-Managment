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

// Represents a node in the singly-linked free frame list.
// In real OS kernels, the next pointer is stored directly within the unallocated physical frame itself.
struct FreeFrameNode {
    int frame_number;
    FreeFrameNode* next;
    FreeFrameNode(int fn, FreeFrameNode* nxt = nullptr) : frame_number(fn), next(nxt) {}
};

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

    // Invalidates any cached entry matching the given virtual page number (TLB shootdown).
    void invalidate(u64 vpn);
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
// Handles address translation, page fault servicing, free frame tracking via a linked list,
// page eviction (FIFO/LRU/Clock), TLB shootdowns, and cache invalidation on page eviction.
class VirtualMemory {
private:
    std::vector<PageTableEntry> page_table;
    std::vector<int> frame_table;
    FreeFrameNode* free_frame_head = nullptr;
    u64 total_frames;
    u64 access_counter = 0;
    u64 page_faults = 0;
    u64 page_hits = 0;
    u64 disk_accesses = 0;
    
    PageReplacementAlgo policy;
    int clock_hand = 0;
    MemoryHierarchy* cache_ptr; 

    // Retrieves next available physical frame from the free-frame linked list in O(1). Returns -1 if empty.
    int find_free_frame();

    // Adds a deallocated physical frame back to the head of the free-frame linked list.
    void release_frame(int frame_num);

    // Selects a victim page based on replacement policy, flushes TLB/cache, and evicts it.
    int evict_page(TLB& tlb);

public:
    // Initializes MMU with physical frames, links all initial free frames, and connects cache.
    VirtualMemory(MemoryHierarchy* cache, PageReplacementAlgo p = VM_LRU);
    ~VirtualMemory();

    // Updates the active page replacement algorithm (FIFO, LRU, CLOCK).
    void set_replacement_policy(PageReplacementAlgo p);

    // Translates virtual address to physical address via TLB/PageTable, handling page faults.
    ll translate(u64 v_addr, bool is_write, TLB& tlb, std::string& report);

    // Displays VM hits, faults, and disk I/O metrics.
    void get_statistics();
};
