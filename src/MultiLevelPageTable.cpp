#include "MultiLevelPageTable.h"
#include <iostream>
#include <iomanip>

// Number of entries at each level: 2^ML_LEVEL_BITS[level]
static int entries_at_level(int level) {
    return 1 << ML_LEVEL_BITS[level];
}

// ---------- index extraction ----------------------------------
// The VPN is decomposed MSB-first:
//   VPN bits [17..15] → PML4 index  (3 bits)
//   VPN bits [14..10] → PDPT index  (5 bits)
//   VPN bits [ 9.. 5] → PD   index  (5 bits)
//   VPN bits [ 4.. 0] → PT   index  (5 bits)

int MultiLevelPageTable::extract_index(u64 vpn, int level) {
    // Calculate the bit-shift: sum of bits for all levels below this one
    int shift = 0;
    for (int i = level + 1; i < PT_LEVELS; ++i)
        shift += ML_LEVEL_BITS[i];

    int mask = (1 << ML_LEVEL_BITS[level]) - 1;
    return static_cast<int>((vpn >> shift) & mask);
}

// ---------- constructor / destructor --------------------------

MultiLevelPageTable::MultiLevelPageTable() {
    pml4 = new PTNode(entries_at_level(0));
    tables_allocated = 1;
}

MultiLevelPageTable::~MultiLevelPageTable() {
    delete pml4;
    pml4 = nullptr;
}

// ---------- walk ----------------------------------------------
// Traverses PML4 → PDPT → PD → PT, counting one memory access per
// level.  Intermediate tables are allocated on demand so that the page
// table remains sparse.

PTNode::Entry* MultiLevelPageTable::walk(u64 vpn, int& walk_cost) {
    total_walks++;
    walk_cost = 0;

    // Guard: VPN must fit in ML_TOTAL_VPN_BITS
    if (vpn >= (1ULL << ML_TOTAL_VPN_BITS))
        return nullptr;

    PTNode* current = pml4;

    // Walk levels 0..2 (interior nodes)
    for (int level = 0; level < PT_LEVELS - 1; ++level) {
        walk_cost++;                           // 1 memory access per level
        int idx = extract_index(vpn, level);
        PTNode::Entry& entry = current->entries[idx];

        if (!entry.next_level) {
            // Allocate the next-level table on demand
            entry.next_level = new PTNode(entries_at_level(level + 1));
            tables_allocated++;
        }
        current = entry.next_level;
    }

    // Level 3 (PT — leaf level)
    walk_cost++;                               // 1 more memory access
    int leaf_idx = extract_index(vpn, PT_LEVELS - 1);
    total_walk_cost += walk_cost;
    return &current->entries[leaf_idx];
}

// ---------- map -----------------------------------------------

void MultiLevelPageTable::map(u64 vpn, int frame, u64 access_time, bool dirty) {
    int cost = 0;
    PTNode::Entry* pte = walk(vpn, cost);
    if (!pte) return;

    pte->valid      = true;
    pte->dirty      = dirty;
    pte->referenced = true;
    pte->frame      = frame;
    pte->last_access = access_time;
    pte->load_time   = access_time;
}

// ---------- unmap ---------------------------------------------

void MultiLevelPageTable::unmap(u64 vpn) {
    int cost = 0;
    PTNode::Entry* pte = walk(vpn, cost);
    if (!pte) return;

    pte->valid      = false;
    pte->frame      = -1;
    pte->dirty      = false;
    pte->referenced = false;
}

// ---------- statistics ----------------------------------------

void MultiLevelPageTable::get_statistics() const {
    std::cout << "\n--- 4-Level Page Table Statistics ---\n";
    std::cout << "Total Page Walks     : " << total_walks << "\n";
    std::cout << "Total Walk Cost      : " << total_walk_cost << " memory accesses\n";
    std::cout << "Avg Walk Cost        : " << std::fixed << std::setprecision(2)
              << get_avg_walk_cost() << " accesses/walk\n";
    std::cout << "Page Tables Allocated: " << tables_allocated << "\n";
    std::cout << "------------------------------------\n";
}

// ---------- clear ---------------------------------------------

void MultiLevelPageTable::clear() {
    delete pml4;
    pml4 = new PTNode(entries_at_level(0));
    total_walks = total_walk_cost = 0;
    tables_allocated = 1;
}
