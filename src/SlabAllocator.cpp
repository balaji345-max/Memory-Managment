#include "SlabAllocator.h"
#include <iostream>
#include <iomanip>

// ---------- SlabCache helper methods ----------

size_t SlabCache::total_used_slots() const {
    size_t used = 0;
    for (auto* s : partial) used += s->num_used;
    for (auto* s : full)    used += s->num_used;
    // empty slabs have 0 used
    return used;
}

size_t SlabCache::total_capacity_slots() const {
    size_t cap = 0;
    size_t sps = slab_size / object_size;
    cap = (partial.size() + full.size() + empty.size()) * sps;
    return cap;
}

// ---------- SlabAllocator ----------

SlabAllocator::SlabAllocator() : total_size(0), next_slab_address(0), next_id(1) {}

SlabAllocator::~SlabAllocator() {
    // SlabCache destructors handle slab deletion
}

void SlabAllocator::init(size_t mem_size) {
    // Clean up old state
    id_map.clear();
    for (size_t i = 0; i < NUM_CACHES; i++) {
        // Manually clean each cache's slabs
        for (auto* s : caches[i].partial) delete s;
        for (auto* s : caches[i].full)    delete s;
        for (auto* s : caches[i].empty)   delete s;
        caches[i].partial.clear();
        caches[i].full.clear();
        caches[i].empty.clear();
        caches[i].total_allocations = 0;
        caches[i].total_frees = 0;
    }

    total_size = mem_size;
    next_slab_address = 0;
    next_id = 1;

    // Initialize each cache with its object size
    for (size_t i = 0; i < NUM_CACHES; i++) {
        caches[i].object_size = CACHE_SIZES[i];
        caches[i].slab_size = SLAB_SIZE;
    }

    std::cout << "[System] Slab Allocator Initialized: " << mem_size << " bytes.\n";
    std::cout << "         Caches: ";
    for (size_t i = 0; i < NUM_CACHES; i++) {
        std::cout << CACHE_SIZES[i];
        if (i < NUM_CACHES - 1) std::cout << ", ";
    }
    std::cout << " bytes  |  Slab size: " << SLAB_SIZE << " bytes\n";
}

int SlabAllocator::find_cache_index(size_t size) {
    for (size_t i = 0; i < NUM_CACHES; i++) {
        if (CACHE_SIZES[i] >= size) return static_cast<int>(i);
    }
    return -1; // Too large for any slab cache
}

Slab* SlabAllocator::create_slab(size_t cache_idx) {
    // Check if we have enough space left in the simulated memory
    if (next_slab_address + SLAB_SIZE > total_size) {
        return nullptr; // No room for another slab
    }

    Slab* new_slab = new Slab(next_slab_address, CACHE_SIZES[cache_idx], SLAB_SIZE);
    next_slab_address += SLAB_SIZE;
    return new_slab;
}

int SlabAllocator::allocate(size_t mem_size, Alloc_Algo) {
    if (mem_size == 0) return -1;

    int cache_idx = find_cache_index(mem_size);
    if (cache_idx == -1) {
        std::cout << "[Slab] Error: Size " << mem_size
                  << " exceeds max cache size (" << CACHE_SIZES[NUM_CACHES - 1] << ").\n";
        return -1;
    }

    SlabCache& cache = caches[cache_idx];
    Slab* target_slab = nullptr;
    int slot = -1;

    // 1. Try to allocate from a partial slab first
    if (!cache.partial.empty()) {
        target_slab = cache.partial.front();
        slot = target_slab->alloc_slot();

        // If this slab is now full, move it to the full list
        if (target_slab->is_full()) {
            cache.partial.pop_front();
            cache.full.push_back(target_slab);
        }
    }
    // 2. Try an empty slab
    else if (!cache.empty.empty()) {
        target_slab = cache.empty.front();
        cache.empty.pop_front();
        slot = target_slab->alloc_slot();

        // After one allocation it becomes partial (unless 1-slot slab, then full)
        if (target_slab->is_full()) {
            cache.full.push_back(target_slab);
        } else {
            cache.partial.push_back(target_slab);
        }
    }
    // 3. Create a brand new slab
    else {
        target_slab = create_slab(cache_idx);
        if (!target_slab) {
            std::cout << "[Slab] Error: Out of memory, cannot create new slab.\n";
            return -1;
        }
        slot = target_slab->alloc_slot();

        if (target_slab->is_full()) {
            cache.full.push_back(target_slab);
        } else {
            cache.partial.push_back(target_slab);
        }
    }

    if (slot == -1) return -1; // Should not happen if logic is correct

    // Record the allocation
    int id = next_id++;
    size_t addr = target_slab->slot_address(slot);
    id_map[id] = {static_cast<size_t>(cache_idx), target_slab, static_cast<size_t>(slot), addr};
    cache.total_allocations++;

    return id;
}

void SlabAllocator::deallocate(int block_id) {
    auto it = id_map.find(block_id);
    if (it == id_map.end()) {
        std::cout << "[Slab] Error: Block id=" << block_id << " not found.\n";
        return;
    }

    SlabAllocRecord& rec = it->second;
    SlabCache& cache = caches[rec.cache_index];
    Slab* slab = rec.slab;

    bool was_full = slab->is_full();
    slab->free_slot(rec.slot_index);
    cache.total_frees++;

    if (was_full) {
        // Move from full -> partial (or empty)
        cache.full.remove(slab);
        if (slab->is_empty()) {
            cache.empty.push_back(slab);
        } else {
            cache.partial.push_back(slab);
        }
    } else {
        // Was partial; might now be empty
        if (slab->is_empty()) {
            cache.partial.remove(slab);
            cache.empty.push_back(slab);
        }
        // Otherwise stays in partial
    }

    id_map.erase(it);
}

size_t SlabAllocator::get_address(int block_id) {
    auto it = id_map.find(block_id);
    if (it == id_map.end()) return 0;
    return it->second.address;
}

void SlabAllocator::display() {
    std::cout << "=== Slab Allocator Memory Layout ===\n";
    for (size_t i = 0; i < NUM_CACHES; i++) {
        SlabCache& cache = caches[i];
        size_t total = cache.total_slabs();
        if (total == 0) continue; // Skip caches with no slabs allocated yet

        std::cout << "\n--- Cache [" << cache.object_size << " bytes] ---\n";
        std::cout << "  Slabs: " << total
                  << " (partial=" << cache.partial.size()
                  << ", full=" << cache.full.size()
                  << ", empty=" << cache.empty.size() << ")\n";

        // Display each slab's bitmap
        auto print_slab_list = [](const std::list<Slab*>& lst, const std::string& label) {
            for (auto* s : lst) {
                std::cout << "  [" << label << "] Addr=0x"
                          << std::hex << std::uppercase << std::setfill('0') << std::setw(4)
                          << s->base_address << std::dec
                          << "  Slots: " << s->num_used << "/" << s->num_slots << "  Bitmap: |";
                for (size_t j = 0; j < s->num_slots; j++) {
                    std::cout << (s->bitmap[j] ? '#' : '.');
                }
                std::cout << "|\n";
            }
        };

        print_slab_list(cache.partial, "PARTIAL");
        print_slab_list(cache.full,    "FULL   ");
        print_slab_list(cache.empty,   "EMPTY  ");
    }
    std::cout << "====================================\n";
}

void SlabAllocator::get_statistics() {
    std::cout << "\n=== Slab Allocator Statistics ===\n";
    std::cout << "Total Memory Pool : " << total_size << " bytes\n";
    std::cout << "Slab Space Used   : " << next_slab_address << " bytes ("
              << (total_size > 0 ? (double)next_slab_address / total_size * 100.0 : 0.0) << "%)\n";
    std::cout << "Active Allocations: " << id_map.size() << "\n\n";

    size_t total_used_mem = 0;

    std::cout << std::left
              << std::setw(10) << "Cache"
              << std::setw(8)  << "Slabs"
              << std::setw(12) << "Used/Total"
              << std::setw(12) << "Allocs"
              << std::setw(10) << "Frees"
              << std::setw(14) << "Int. Frag"
              << "\n";
    std::cout << std::string(66, '-') << "\n";

    for (size_t i = 0; i < NUM_CACHES; i++) {
        SlabCache& cache = caches[i];
        size_t total = cache.total_slabs();
        if (total == 0 && cache.total_allocations == 0) continue;

        size_t used = cache.total_used_slots();
        size_t cap = cache.total_capacity_slots();

        // Internal fragmentation: for each allocation, waste = cache.object_size - actual_request_size
        // Since we don't store the original request size per-slot, we approximate:
        // We know allocations went to this cache, so min waste per alloc = 0, max = object_size - prev_cache_size
        // For stats, report total wasted capacity = used_slots * object_size - actual bytes requested
        // Since we can't track per-slot request, report the slot-level utilization
        size_t used_bytes = used * cache.object_size;
        total_used_mem += used_bytes;

        std::cout << std::left
                  << std::setw(10) << (std::to_string(cache.object_size) + "B")
                  << std::setw(8)  << total
                  << std::setw(12) << (std::to_string(used) + "/" + std::to_string(cap))
                  << std::setw(12) << cache.total_allocations
                  << std::setw(10) << cache.total_frees
                  << std::setw(14) << (used_bytes > 0 ? std::to_string(used_bytes) + "B" : "-")
                  << "\n";
    }

    // Overall fragmentation: slab space allocated but not used by any object
    size_t slab_waste = next_slab_address - total_used_mem;
    double util = next_slab_address > 0 ? (double)total_used_mem / next_slab_address * 100.0 : 0.0;

    std::cout << std::string(66, '-') << "\n";
    std::cout << "Slab Space Utilization: " << std::fixed << std::setprecision(1) << util << "%\n";
    std::cout << "Slab Internal Waste   : " << slab_waste << " bytes (unused slots in allocated slabs)\n";
    std::cout << "=================================\n";
}
