#pragma once
#include "Allocator.h"
#include <vector>
#include <list>
#include <unordered_map>

// ---------- Slab: a single contiguous page of fixed-size slots ----------
struct Slab {
    size_t base_address;       // Start address within simulated memory
    size_t object_size;        // Size of each slot
    size_t num_slots;          // Total slots in this slab
    size_t num_used;           // How many slots are currently allocated
    std::vector<bool> bitmap;  // true = occupied, false = free

    Slab(size_t base, size_t obj_sz, size_t slab_sz)
        : base_address(base), object_size(obj_sz),
          num_slots(slab_sz / obj_sz), num_used(0),
          bitmap(slab_sz / obj_sz, false) {}

    // Returns slot index, or -1 if full
    int alloc_slot() {
        for (size_t i = 0; i < num_slots; i++) {
            if (!bitmap[i]) {
                bitmap[i] = true;
                num_used++;
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    // Frees a slot by index
    void free_slot(size_t slot_idx) {
        if (slot_idx < num_slots && bitmap[slot_idx]) {
            bitmap[slot_idx] = false;
            num_used--;
        }
    }

    bool is_full() const { return num_used == num_slots; }
    bool is_empty() const { return num_used == 0; }

    // Address of a specific slot
    size_t slot_address(size_t slot_idx) const {
        return base_address + slot_idx * object_size;
    }
};

// ---------- SlabCache: manages all slabs for one object size ----------
struct SlabCache {
    size_t object_size;       // Fixed object size for this cache
    size_t slab_size;         // Bytes per slab (e.g. 1024)

    std::list<Slab*> partial; // Slabs with some free slots
    std::list<Slab*> full;    // Slabs with no free slots
    std::list<Slab*> empty;   // Slabs with all slots free

    // Statistics
    size_t total_allocations = 0;
    size_t total_frees = 0;

    SlabCache() : object_size(0), slab_size(0) {}
    SlabCache(size_t obj_sz, size_t sl_sz) : object_size(obj_sz), slab_size(sl_sz) {}

    ~SlabCache() {
        for (auto* s : partial) delete s;
        for (auto* s : full) delete s;
        for (auto* s : empty) delete s;
    }

    size_t slots_per_slab() const { return slab_size / object_size; }
    size_t total_slabs() const { return partial.size() + full.size() + empty.size(); }
    size_t total_used_slots() const;
    size_t total_capacity_slots() const;
};

// ---------- SlabAllocator: the allocator with multiple caches ----------

// Allocation record: maps block ID -> which cache + which slab + which slot
struct SlabAllocRecord {
    size_t cache_index;   // Index into cache_sizes array
    Slab* slab;           // Pointer to the slab
    size_t slot_index;    // Slot within the slab
    size_t address;       // Computed address
};

class SlabAllocator : public Allocator {
private:
    static constexpr size_t NUM_CACHES = 7;
    static constexpr size_t CACHE_SIZES[NUM_CACHES] = {8, 16, 32, 64, 128, 256, 512};
    static constexpr size_t SLAB_SIZE = 1024; // Bytes per slab

    size_t total_size;
    size_t next_slab_address;  // Next free address to carve out a new slab
    int next_id;

    SlabCache caches[NUM_CACHES];
    std::unordered_map<int, SlabAllocRecord> id_map; // block_id -> record

    // Find the smallest cache that fits the requested size
    int find_cache_index(size_t size);

    // Create a new slab for a given cache, returns nullptr if no space left
    Slab* create_slab(size_t cache_idx);

public:
    SlabAllocator();
    ~SlabAllocator();

    void init(size_t mem_size) override;
    int allocate(size_t mem_size, Alloc_Algo algo = Firstfit) override;
    void deallocate(int block_id) override;
    size_t get_address(int block_id) override;
    void display() override;
    void get_statistics() override;
};
