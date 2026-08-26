#include "VirtualMemory.h"
#include <iostream>
#include <iomanip>
#include <limits>

// ==================== TLB ====================

// Configures sets and ways for the Translation Lookaside Buffer.
TLB::TLB(int num_entries, int assoc) : ways(assoc) {
    sets = num_entries / ways;
    table.resize(sets, std::vector<TLBEntry>(ways));
}

// Searches TLB set for matching VPN. Returns physical frame number on hit, or -1 on miss.
int TLB::lookup(u64 vpn) {
    SpinLockGuard guard(tlb_lock);
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
    SpinLockGuard guard(tlb_lock);
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
    SpinLockGuard guard(tlb_lock);
    int idx = vpn % sets;
    for (auto& entry : table[idx]) {
        if (entry.valid && entry.vpn == vpn) {
            entry.valid = false;
            break;
        }
    }
}

// ==================== VirtualMemory ====================

// Initializes page table, frame tracking, and builds the linked list of all available free physical frames.
VirtualMemory::VirtualMemory(MemoryHierarchy* cache, PageReplacementAlgo p) 
    : policy(p), cache_ptr(cache) {
    total_frames = PHYSICAL_MEM_SIZE / PAGE_SIZE;
    page_table.resize(VIRTUAL_MEM_SIZE / PAGE_SIZE);
    frame_table.assign(total_frames, -1);

    // Build free frame linked list in ascending order: [0] -> [1] -> ... -> [total_frames - 1] -> nullptr
    for (int i = static_cast<int>(total_frames) - 1; i >= 0; --i) {
        free_frame_head = new FreeFrameNode(i, free_frame_head);
    }
}

// Cleans up any remaining nodes in the free frame linked list.
VirtualMemory::~VirtualMemory() {
    while (free_frame_head) {
        FreeFrameNode* temp = free_frame_head;
        free_frame_head = free_frame_head->next;
        delete temp;
    }
}

void VirtualMemory::set_multi_level_paging(bool enable) {
    use_multi_level = enable;
    if (enable) {
        std::cout << "[MMU] Switched to 4-level page table (24-bit VA, "
                  << ML_VIRTUAL_MEM_SIZE << " bytes virtual).\n";
    } else {
        std::cout << "[MMU] Switched to flat page table (" 
                  << VIRTUAL_MEM_SIZE << " bytes virtual).\n";
    }
}

// Sets the page replacement policy to LRU, FIFO, or CLOCK.
void VirtualMemory::set_replacement_policy(PageReplacementAlgo p) {
    policy = p;
}

// Retrieves and removes the head frame from the free-frame linked list in O(1). Returns -1 if empty.
int VirtualMemory::find_free_frame() {
    if (!free_frame_head) {
        return -1;
    }
    FreeFrameNode* node = free_frame_head;
    int frame = node->frame_number;
    free_frame_head = free_frame_head->next;
    delete node;
    return frame;
}

// Inserts a freed physical frame back to the front of the free-frame linked list in O(1).
void VirtualMemory::release_frame(int frame_num) {
    free_frame_head = new FreeFrameNode(frame_num, free_frame_head);
}

// Evicts a page using FIFO, LRU, or CLOCK policy, invalidates TLB/cache lines, and writes back dirty pages.
int VirtualMemory::evict_page(TLB& tlb) {
    int v_f = -1;
    int v_p = -1;

    if (policy == VM_LRU || policy == VM_FIFO) {
        u64 min_t = std::numeric_limits<u64>::max();
        for (int i = 0; i < static_cast<int>(total_frames); i++) {
            int p_idx = frame_table[i];
            if (p_idx < 0) continue;

            u64 t = 0;
            if (use_multi_level) {
                int cost = 0;
                PTNode::Entry* pte = multi_level_pt.walk(static_cast<u64>(p_idx), cost);
                if (pte && pte->valid) {
                    t = (policy == VM_LRU) ? pte->last_access : pte->load_time;
                }
            } else {
                t = (policy == VM_LRU) ? page_table[p_idx].last_access_time : page_table[p_idx].loaded_time;
            }

            if (t < min_t) {
                min_t = t;
                v_f = i;
                v_p = p_idx;
            }
        }
    } else {
        // CLOCK algorithm
        int attempts = 0;
        while (attempts < static_cast<int>(total_frames) * 2) {
            int p_idx = frame_table[clock_hand];
            if (p_idx >= 0) {
                bool ref = false;
                if (use_multi_level) {
                    int cost = 0;
                    PTNode::Entry* pte = multi_level_pt.walk(static_cast<u64>(p_idx), cost);
                    if (pte) ref = pte->referenced;
                } else {
                    ref = page_table[p_idx].referenced;
                }

                if (ref) {
                    if (use_multi_level) {
                        int cost = 0;
                        PTNode::Entry* pte = multi_level_pt.walk(static_cast<u64>(p_idx), cost);
                        if (pte) pte->referenced = false;
                    } else {
                        page_table[p_idx].referenced = false;
                    }
                    clock_hand = (clock_hand + 1) % total_frames;
                } else {
                    v_f = clock_hand;
                    v_p = p_idx;
                    clock_hand = (clock_hand + 1) % total_frames;
                    break;
                }
            } else {
                clock_hand = (clock_hand + 1) % total_frames;
            }
            attempts++;
        }
    }

    if (v_f == -1 || v_p == -1) return -1;
    
    // Invalidate stale translation in the TLB (TLB shootdown)
    tlb.invalidate(static_cast<u64>(v_p));

    // Invalidate cache lines associated with this physical frame
    if (cache_ptr) {
        u64 physical_addr = static_cast<u64>(v_f) * PAGE_SIZE;
        cache_ptr->invalidate_physical_range(physical_addr, PAGE_SIZE);
    }

    // Write back to simulated disk if page was dirty
    bool dirty = false;
    if (use_multi_level) {
        int cost = 0;
        PTNode::Entry* pte = multi_level_pt.walk(static_cast<u64>(v_p), cost);
        if (pte) { dirty = pte->dirty; }
        multi_level_pt.unmap(static_cast<u64>(v_p));
    } else {
        dirty = page_table[v_p].dirty;
        page_table[v_p].valid = false;
    }
    if (dirty) disk_accesses++;
    
    frame_table[v_f] = -1;
    return v_f;
}

// Performs full MMU translation (TLB -> Page Table -> Page Fault handling) and returns physical address.
ll VirtualMemory::translate(u64 v_addr, bool is_write, TLB& tlb, std::string& report) {
    std::lock_guard<std::mutex> pt_lock(page_table_mutex);

    // Bounds check depends on paging mode
    u64 max_addr = use_multi_level ? ML_VIRTUAL_MEM_SIZE : VIRTUAL_MEM_SIZE;
    
    // Check mmap regions for addresses beyond normal range or for protection
    if (v_addr >= max_addr) {
        if (mmap_mgr && mmap_mgr->contains(v_addr)) {
            if (!mmap_mgr->check_access(v_addr, is_write)) {
                report = "Protection Fault (mmap region)";
                return -1;
            }
            // For mmap regions beyond normal VA, we can't map them in the flat PT.
            // They require multi-level PT.
            if (!use_multi_level) {
                report = "Segmentation Fault (enable multilevel for large mmap)";
                return -1;
            }
        } else {
            report = "Segmentation Fault (Address Out of Bounds)";
            return -1;
        }
    }

    // Check mmap protection for addresses within normal range
    if (mmap_mgr && mmap_mgr->contains(v_addr)) {
        if (!mmap_mgr->check_access(v_addr, is_write)) {
            report = "Protection Fault (mmap region)";
            return -1;
        }
    }

    access_counter++;
    u64 vpn = v_addr / PAGE_SIZE;
    u64 offset = v_addr % PAGE_SIZE;

    // Step 1: TLB lookup
    int pfn = tlb.lookup(vpn);
    if (pfn != -1) {
        report = "TLB Hit";
        page_hits++;
        // Update access metadata
        if (use_multi_level) {
            int cost = 0;
            PTNode::Entry* pte = multi_level_pt.walk(vpn, cost);
            if (pte) {
                pte->last_access = access_counter;
                pte->referenced = true;
                if (is_write) pte->dirty = true;
            }
        } else {
            page_table[vpn].last_access_time = access_counter;
            page_table[vpn].referenced = true;
            if (is_write) page_table[vpn].dirty = true;
        }
        return static_cast<ll>(pfn * PAGE_SIZE + offset);
    }

    // Step 2: Page table lookup
    bool page_valid = false;
    int frame_num = -1;

    if (use_multi_level) {
        int walk_cost = 0;
        PTNode::Entry* pte = multi_level_pt.walk(vpn, walk_cost);
        if (pte && pte->valid) {
            page_valid = true;
            frame_num = pte->frame;
            pte->last_access = access_counter;
            pte->referenced = true;
            if (is_write) pte->dirty = true;
        }
    } else {
        if (vpn < page_table.size() && page_table[vpn].valid) {
            page_valid = true;
            frame_num = page_table[vpn].frame_number;
            page_table[vpn].last_access_time = access_counter;
            page_table[vpn].referenced = true;
            if (is_write) page_table[vpn].dirty = true;
        }
    }

    if (page_valid) {
        report = "Page Table Hit";
        page_hits++;
        tlb.insert(vpn, frame_num);
        return static_cast<ll>(frame_num * PAGE_SIZE + offset);
    }

    // Step 3: Page fault
    report = "Page Fault";
    page_faults++;
    disk_accesses++;

    int f = find_free_frame();
    if (f == -1) f = evict_page(tlb);
    if (f == -1) {
        report = "Page Fault (no frames available)";
        return -1;
    }

    // Create new mapping
    if (use_multi_level) {
        multi_level_pt.map(vpn, f, access_counter, is_write);
    } else {
        page_table[vpn] = {true, is_write, true, f, access_counter, access_counter};
    }

    frame_table[f] = static_cast<int>(vpn);
    tlb.insert(vpn, f);
    return static_cast<ll>(f * PAGE_SIZE + offset);
}

// Prints page hits, page faults, and disk access count.
void VirtualMemory::get_statistics() {
    std::cout << "VM: Hits=" << page_hits << ", Faults=" << page_faults << ", Disk=" << disk_accesses << "\n";
    if (use_multi_level) {
        multi_level_pt.get_statistics();
    }
    if (mmap_mgr) {
        mmap_mgr->get_statistics();
    }
}

void VirtualMemory::reset_stats() {
    page_hits = page_faults = disk_accesses = access_counter = 0;
}