#pragma once
#include "Allocator.h"
#include <vector>
#include <list>
#include <unordered_map>

// Represents a contiguous memory slab partitioned into fixed-size slots.
struct Slab {
    size_t base_address;
    size_t object_size;
    size_t num_slots;
    size_t num_used;
    std::vector<bool> bitmap;

    Slab(size_t base, size_t obj_sz, size_t slab_sz)
        : base_address(base), object_size(obj_sz),
          num_slots(slab_sz / obj_sz), num_used(0),
          bitmap(slab_sz / obj_sz, false) {}

    // Allocates the first available slot in the slab. Returns slot index or -1 if full.
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

    // Marks the specified slot as free.
    void free_slot(size_t slot_idx) {
        if (slot_idx < num_slots && bitmap[slot_idx]) {
            bitmap[slot_idx] = false;
            num_used--;
        }
    }

    // Checks whether all slots in this slab are occupied.
    bool is_full() const { return num_used == num_slots; }

    // Checks whether all slots in this slab are free.
    bool is_empty() const { return num_used == 0; }

    // Calculates the start memory address for a given slot index.
    size_t slot_address(size_t slot_idx) const {
        return base_address + slot_idx * object_size;
    }
};

// Manages partial, full, and empty slabs for a specific object size.
struct SlabCache {
    size_t object_size;
    size_t slab_size;

    std::list<Slab*> partial;
    std::list<Slab*> full;
    std::list<Slab*> empty;

    size_t total_allocations = 0;
    size_t total_frees = 0;

    SlabCache() : object_size(0), slab_size(0) {}
    SlabCache(size_t obj_sz, size_t sl_sz) : object_size(obj_sz), slab_size(sl_sz) {}

    ~SlabCache() {
        for (auto* s : partial) delete s;
        for (auto* s : full) delete s;
        for (auto* s : empty) delete s;
    }

    // Returns total slot capacity per individual slab.
    size_t slots_per_slab() const { return slab_size / object_size; }

    // Returns total count of allocated slabs across partial, full, and empty lists.
    size_t total_slabs() const { return partial.size() + full.size() + empty.size(); }

    // Returns number of currently used slots across all slabs in this cache.
    size_t total_used_slots() const;

    // Returns total slot capacity across all slabs in this cache.
    size_t total_capacity_slots() const;
};

// Metadata mapping an allocated block ID to its cache, slab, and slot index.
struct SlabAllocRecord {
    size_t cache_index;
    Slab* slab;
    size_t slot_index;
    size_t address;
};

// Kernel-style Slab Allocator.
// Provides dedicated caches for power-of-two object sizes (8B to 512B) to eliminate internal fragmentation.
class SlabAllocator : public Allocator {
private:
    static constexpr size_t NUM_CACHES = 7;
    static constexpr size_t CACHE_SIZES[NUM_CACHES] = {8, 16, 32, 64, 128, 256, 512};
    static constexpr size_t SLAB_SIZE = 1024;

    size_t total_size;
    size_t next_slab_address;
    int next_id;

    SlabCache caches[NUM_CACHES];
    std::unordered_map<int, SlabAllocRecord> id_map;

    // Finds the index of the smallest cache size that fits the requested size.
    int find_cache_index(size_t size);

    // Carves out a new slab from the memory pool for the given cache.
    Slab* create_slab(size_t cache_idx);

public:
    SlabAllocator();
    ~SlabAllocator() override;

    // Resets slab caches and prepares memory pool.
    void init(size_t mem_size) override;

    // Allocates from partial slab -> empty slab -> new slab.
    int allocate(size_t mem_size, Alloc_Algo algo = Firstfit) override;

    // Returns slot to its slab and updates cache list placement.
    void deallocate(int block_id) override;

    // Returns virtual address of the allocated slot.
    size_t get_address(int block_id) override;

    // Displays per-cache slab lists and occupancy bitmaps.
    void display() override;

    // Outputs slab memory utilization, allocation counts, and waste statistics.
    void get_statistics() override;
};
