#pragma once
#include <vector>
#include <cstdint>
#include <iostream>
#include <iomanip>
#include "Allocator.h"

using u64 = uint64_t;

// ---------- x86-style 4-level page table configuration ----------
// Scaled-down 24-bit virtual address to keep the simulation manageable:
//   [PML4:3][PDPT:5][PD:5][PT:5][Offset:6]
// This yields a 16 MB virtual address space with 64-byte pages.

constexpr int PT_LEVELS        = 4;
constexpr int ML_OFFSET_BITS   = 6;                          // log2(PAGE_SIZE)
constexpr int ML_LEVEL_BITS[PT_LEVELS] = {3, 5, 5, 5};       // PML4, PDPT, PD, PT
constexpr int ML_TOTAL_VPN_BITS = 3 + 5 + 5 + 5;             // 18
constexpr int ML_TOTAL_VA_BITS  = ML_OFFSET_BITS + ML_TOTAL_VPN_BITS; // 24
constexpr u64 ML_VIRTUAL_MEM_SIZE  = 1ULL << ML_TOTAL_VA_BITS;  // 16 MB
constexpr u64 ML_PHYSICAL_MEM_SIZE = 64ULL * 1024;               // 64 KB

// ---------- page-table node -----------------------------------

struct PTNode {
    // A leaf entry stores a frame mapping; an interior entry stores a
    // pointer to the next-level PTNode.
    struct Entry {
        bool      valid      = false;
        bool      dirty      = false;
        bool      referenced = false;
        int       frame      = -1;          // leaf only
        PTNode*   next_level = nullptr;     // interior only
        u64       last_access = 0;
        u64       load_time   = 0;
    };

    int                 num_entries;
    std::vector<Entry>  entries;

    explicit PTNode(int n) : num_entries(n), entries(n) {}

    // Recursively delete child tables
    ~PTNode() {
        for (auto& e : entries) {
            delete e.next_level;
            e.next_level = nullptr;
        }
    }
};

// ---------- 4-level page table --------------------------------

class MultiLevelPageTable {
private:
    PTNode* pml4 = nullptr;               // CR3 — root table
    u64 total_walks       = 0;
    u64 total_walk_cost   = 0;             // cumulative memory accesses for all walks
    u64 tables_allocated  = 0;

    // Extracts the index for a given level from a virtual page number.
    static int extract_index(u64 vpn, int level);

public:
    MultiLevelPageTable();
    ~MultiLevelPageTable();

    // Walks all four levels, allocating intermediate tables on demand.
    // Returns the leaf Entry (or nullptr on out-of-range VPN).
    // walk_cost is set to the number of memory accesses performed.
    PTNode::Entry* walk(u64 vpn, int& walk_cost);

    // Creates a full mapping from VPN → physical frame.
    void map(u64 vpn, int frame, u64 access_time, bool dirty = false);

    // Invalidates the leaf entry for a VPN.
    void unmap(u64 vpn);

    // Accessors for benchmark / stats
    u64  get_total_walks()     const { return total_walks; }
    u64  get_total_walk_cost() const { return total_walk_cost; }
    double get_avg_walk_cost() const {
        return total_walks > 0 ? (double)total_walk_cost / total_walks : 0.0;
    }
    u64  get_tables_allocated() const { return tables_allocated; }

    void get_statistics() const;
    void clear();
};
