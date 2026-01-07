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

struct TLBEntry {
    bool valid = false;
    u64 vpn = 0, pfn = 0, last_access = 0;
};

class TLB {
public:
    int sets, ways; u64 timer = 0;
    std::vector<std::vector<TLBEntry>> table;
    TLB(int num_entries, int assoc);
    int lookup(u64 vpn);
    void insert(u64 vpn, u64 pfn);
};

struct PageTableEntry {
    bool valid = false, dirty = false, referenced = false;
    int frame_number = -1;
    u64 last_access_time = 0, loaded_time = 0;
};


class VirtualMemory {
private:
    std::vector<PageTableEntry> page_table;
    std::vector<int> frame_table;
    u64 total_frames, access_counter = 0;
    u64 page_faults = 0, page_hits = 0, disk_accesses = 0;
    
    PageReplacementAlgo policy;
    int clock_hand = 0;
    MemoryHierarchy* cache_ptr; 

    int find_free_frame();
    int evict_page();

public:
    VirtualMemory(MemoryHierarchy* cache, PageReplacementAlgo p = VM_LRU);
    void set_replacement_policy(PageReplacementAlgo p);
    ll translate(u64 v_addr, bool is_write, TLB& tlb, std::string& report);
    void get_statistics();
};

