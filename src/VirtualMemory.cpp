#include "VirtualMemory.h"
#include <iostream>
#include <iomanip>
#include <limits>

TLB::TLB(int num_entries, int assoc) : ways(assoc) {
    sets = num_entries / ways;
    table.resize(sets, std::vector<TLBEntry>(ways));
}

int TLB::lookup(u64 vpn) {
    timer++;
    int idx = vpn % sets;
    for (auto& entry : table[idx]) {
        if (entry.valid && entry.vpn == vpn) {
            entry.last_access = timer;
            return (int)entry.pfn;
        }
    }
    return -1;
}

void TLB::insert(u64 vpn, u64 pfn) {
    int idx = vpn % sets;
    int victim = 0; u64 min_t = std::numeric_limits<u64>::max();
    for (int i = 0; i < ways; i++) {
        if (!table[idx][i].valid) { victim = i; break; }
        if (table[idx][i].last_access < min_t) { min_t = table[idx][i].last_access; victim = i; }
    }
    table[idx][victim] = {true, vpn, pfn, timer};
}

VirtualMemory::VirtualMemory(MemoryHierarchy* cache, PageReplacementAlgo p) 
    : policy(p), cache_ptr(cache) {
    total_frames = PHYSICAL_MEM_SIZE / PAGE_SIZE;
    page_table.resize(VIRTUAL_MEM_SIZE / PAGE_SIZE);
    frame_table.assign(total_frames, -1);
}

void VirtualMemory::set_replacement_policy(PageReplacementAlgo p) {
    policy = p;
}

int VirtualMemory::find_free_frame() {
    for (int i = 0; i < (int)total_frames; i++) if (frame_table[i] == -1) return i;
    return -1;
}

int VirtualMemory::evict_page() {
    int v_f = -1, v_p = -1;
    if (policy == VM_LRU || policy == VM_FIFO) {
        u64 min_t = std::numeric_limits<u64>::max();
        for (int i = 0; i < (int)total_frames; i++) {
            int p_idx = frame_table[i];
            u64 t = (policy == VM_LRU) ? page_table[p_idx].last_access_time : page_table[p_idx].loaded_time;
            if (t < min_t) { min_t = t; v_f = i; v_p = p_idx; }
        }
    } else {
        while (true) {
            int p_idx = frame_table[clock_hand];
            if (page_table[p_idx].referenced) {
                page_table[p_idx].referenced = false;
                clock_hand = (clock_hand + 1) % total_frames;
            } else { v_f = clock_hand; v_p = p_idx; clock_hand = (clock_hand + 1) % total_frames; break; }
        }
    }
    
    if (cache_ptr) {
        u64 physical_addr = (u64)v_f * PAGE_SIZE;
        cache_ptr->invalidate_physical_range(physical_addr, PAGE_SIZE);
    }

    if (page_table[v_p].dirty) disk_accesses++;
    page_table[v_p].valid = false;
    frame_table[v_f] = -1;
    return v_f;
}

ll VirtualMemory::translate(u64 v_addr, bool is_write, TLB& tlb, std::string& report) {
    access_counter++;
    u64 vpn = v_addr / PAGE_SIZE, offset = v_addr % PAGE_SIZE;
    int pfn = tlb.lookup(vpn);
    if (pfn != -1) { report = "TLB Hit"; page_hits++; return (ll)(pfn * PAGE_SIZE + offset); }
    if (page_table[vpn].valid) {
        report = "Page Table Hit"; page_hits++;
        page_table[vpn].last_access_time = access_counter;
        page_table[vpn].referenced = true;
        if (is_write) page_table[vpn].dirty = true;
        tlb.insert(vpn, page_table[vpn].frame_number);
        return (ll)(page_table[vpn].frame_number * PAGE_SIZE + offset);
    }
    report = "Page Fault"; page_faults++; disk_accesses++;
    int f = find_free_frame();
    if (f == -1) f = evict_page();
    page_table[vpn] = {true, is_write, true, f, access_counter, access_counter};
    frame_table[f] = (int)vpn;
    tlb.insert(vpn, f);
    return (ll)(f * PAGE_SIZE + offset);
}

void VirtualMemory::get_statistics() {
    std::cout << "VM: Hits=" << page_hits << ", Faults=" << page_faults << ", Disk=" << disk_accesses << "\n";
}