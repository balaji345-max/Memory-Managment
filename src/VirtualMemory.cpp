#include "VirtualMemory.h"
#include <iostream>
#include <iomanip>
#include <limits>

// Configures sets and ways for the Translation Lookaside Buffer.
TLB::TLB(int num_entries, int assoc) : ways(assoc) {
    sets = num_entries / ways;
    table.resize(sets, std::vector<TLBEntry>(ways));
}

// Searches TLB set for matching VPN. Returns physical frame number on hit, or -1 on miss.
int TLB::lookup(u64 vpn) {
    timer++;
    int idx = vpn % sets;
    for (auto& entry : table[idx]) {
        if (entry.valid && entry.vpn == vpn) {
            entry.last_access = timer;
            return static_cast<int>(entry.pfn);
        }
    }
    return -1;
}

// Inserts VPN -> PFN mapping into the TLB, evicting the least recently used entry if full.
void TLB::insert(u64 vpn, u64 pfn) {
    int idx = vpn % sets;

    // If an existing entry already holds this VPN, update it directly
    for (int i = 0; i < ways; i++) {
        if (table[idx][i].valid && table[idx][i].vpn == vpn) {
            table[idx][i].pfn = pfn;
            table[idx][i].last_access = timer;
            return;
        }
    }

    int victim = 0;
    u64 min_t = std::numeric_limits<u64>::max();

    for (int i = 0; i < ways; i++) {
        if (!table[idx][i].valid) {
            victim = i;
            break;
        }
        if (table[idx][i].last_access < min_t) {
            min_t = table[idx][i].last_access;
            victim = i;
        }
    }
    table[idx][victim] = {true, vpn, pfn, timer};
}

// Invalidates any cached TLB entry matching the virtual page number.
void TLB::invalidate(u64 vpn) {
    int idx = vpn % sets;
    for (auto& entry : table[idx]) {
        if (entry.valid && entry.vpn == vpn) {
            entry.valid = false;
            break;
        }
    }
}

// Initializes page table, physical frame tracking, and populates the free frame list.
VirtualMemory::VirtualMemory(MemoryHierarchy* cache, PageReplacementAlgo p) 
    : policy(p), cache_ptr(cache) {
    total_frames = PHYSICAL_MEM_SIZE / PAGE_SIZE;
    page_table.resize(VIRTUAL_MEM_SIZE / PAGE_SIZE);
    frame_table.assign(total_frames, -1);

    for (int i = 0; i < static_cast<int>(total_frames); ++i) {
        free_frame_list.push(i);
    }
}

// Sets the page replacement policy to LRU, FIFO, or CLOCK.
void VirtualMemory::set_replacement_policy(PageReplacementAlgo p) {
    policy = p;
}

// Retrieves the next available physical frame from the free frame list in O(1). Returns -1 if empty.
int VirtualMemory::find_free_frame() {
    if (free_frame_list.empty()) {
        return -1;
    }
    int frame = free_frame_list.front();
    free_frame_list.pop();
    return frame;
}

// Evicts a page using FIFO, LRU, or CLOCK policy, invalidates TLB/cache lines, and writes back dirty pages.
int VirtualMemory::evict_page(TLB& tlb) {
    int v_f = -1;
    int v_p = -1;

    if (policy == VM_LRU || policy == VM_FIFO) {
        u64 min_t = std::numeric_limits<u64>::max();
        for (int i = 0; i < static_cast<int>(total_frames); i++) {
            int p_idx = frame_table[i];
            u64 t = (policy == VM_LRU) ? page_table[p_idx].last_access_time : page_table[p_idx].loaded_time;
            if (t < min_t) {
                min_t = t;
                v_f = i;
                v_p = p_idx;
            }
        }
    } else {
        while (true) {
            int p_idx = frame_table[clock_hand];
            if (page_table[p_idx].referenced) {
                page_table[p_idx].referenced = false;
                clock_hand = (clock_hand + 1) % total_frames;
            } else {
                v_f = clock_hand;
                v_p = p_idx;
                clock_hand = (clock_hand + 1) % total_frames;
                break;
            }
        }
    }
    
    // Invalidate stale translation in the TLB (TLB shootdown)
    tlb.invalidate(static_cast<u64>(v_p));

    // Invalidate cache lines associated with this physical frame
    if (cache_ptr) {
        u64 physical_addr = static_cast<u64>(v_f) * PAGE_SIZE;
        cache_ptr->invalidate_physical_range(physical_addr, PAGE_SIZE);
    }

    // Write back to simulated disk if page was dirty
    if (page_table[v_p].dirty) disk_accesses++;
    page_table[v_p].valid = false;
    frame_table[v_f] = -1;
    return v_f;
}

// Performs full MMU translation (TLB -> Page Table -> Page Fault handling) and returns physical address.
ll VirtualMemory::translate(u64 v_addr, bool is_write, TLB& tlb, std::string& report) {
    if (v_addr >= VIRTUAL_MEM_SIZE) {
        report = "Segmentation Fault (Address Out of Bounds)";
        return -1;
    }

    access_counter++;
    u64 vpn = v_addr / PAGE_SIZE;
    u64 offset = v_addr % PAGE_SIZE;

    int pfn = tlb.lookup(vpn);
    if (pfn != -1) {
        report = "TLB Hit";
        page_hits++;
        page_table[vpn].last_access_time = access_counter;
        page_table[vpn].referenced = true;
        if (is_write) page_table[vpn].dirty = true;
        return static_cast<ll>(pfn * PAGE_SIZE + offset);
    }

    if (page_table[vpn].valid) {
        report = "Page Table Hit";
        page_hits++;
        page_table[vpn].last_access_time = access_counter;
        page_table[vpn].referenced = true;
        if (is_write) page_table[vpn].dirty = true;
        tlb.insert(vpn, page_table[vpn].frame_number);
        return static_cast<ll>(page_table[vpn].frame_number * PAGE_SIZE + offset);
    }

    report = "Page Fault";
    page_faults++;
    disk_accesses++;

    int f = find_free_frame();
    if (f == -1) f = evict_page(tlb);

    page_table[vpn] = {true, is_write, true, f, access_counter, access_counter};
    frame_table[f] = static_cast<int>(vpn);
    tlb.insert(vpn, f);
    return static_cast<ll>(f * PAGE_SIZE + offset);
}

// Prints page hits, page faults, and disk access count.
void VirtualMemory::get_statistics() {
    std::cout << "VM: Hits=" << page_hits << ", Faults=" << page_faults << ", Disk=" << disk_accesses << "\n";
}