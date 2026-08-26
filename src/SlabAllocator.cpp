#include "SlabAllocator.h"
#include <iostream>
#include <iomanip>

// Calculates total occupied slots across all slabs in the cache.
size_t SlabCache::total_used_slots() const {
    size_t used = 0;
    for (auto* s : partial) used += s->num_used;
    for (auto* s : full)    used += s->num_used;
    return used;
}

// Calculates total slot capacity across all slabs in the cache.
size_t SlabCache::total_capacity_slots() const {
    size_t sps = slab_size / object_size;
    return (partial.size() + full.size() + empty.size()) * sps;
}

SlabAllocator::SlabAllocator() : total_size(0), total_pages(0), next_slab_address(0), next_id(1) {}

SlabAllocator::~SlabAllocator() = default;

// Resets slab caches, frees existing slabs, and prepares pool for new allocations.
void SlabAllocator::init(size_t mem_size) {
    std::lock_guard<std::mutex> pool_lock(pool_mutex);
    std::lock_guard<std::mutex> id_lock(id_map_mutex);

    id_map.clear();
    for (size_t i = 0; i < NUM_CACHES; i++) {
        std::lock_guard<std::mutex> cache_lock(caches[i].cache_mutex);
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
    total_pages = mem_size / PAGE_SIZE;
    next_slab_address = 0;
    next_id = 1;

    for (size_t i = 0; i < NUM_CACHES; i++) {
        caches[i].object_size = CACHE_SIZES[i];
        caches[i].slab_size = SLAB_SIZE;
    }

    std::cout << "[System] Page-Backed Slab Allocator Initialized: " << mem_size << " bytes ("
              << total_pages << " pages).\n";
    std::cout << "         Caches: ";
    for (size_t i = 0; i < NUM_CACHES; i++) {
        std::cout << CACHE_SIZES[i];
        if (i < NUM_CACHES - 1) std::cout << ", ";
    }
    std::cout << " bytes  |  Slab size: " << SLAB_SIZE << " bytes (" << PAGES_PER_SLAB << " pages)\n";
}

// Finds the index of the first cache capable of holding an object of given size.
int SlabAllocator::find_cache_index(size_t size) {
    for (size_t i = 0; i < NUM_CACHES; i++) {
        if (CACHE_SIZES[i] >= size) return static_cast<int>(i);
    }
    return -1;
}

// Allocates a new contiguous 16-page slab from the system pool for the designated cache.
Slab* SlabAllocator::create_slab(size_t cache_idx) {
    // pool_mutex must be held by caller
    if (next_slab_address + SLAB_SIZE > total_size) {
        return nullptr;
    }

    Slab* new_slab = new Slab(next_slab_address, CACHE_SIZES[cache_idx]);
    next_slab_address += SLAB_SIZE;
    return new_slab;
}

// Allocates a slot matching the requested size from partial, empty, or newly created slabs.
// Locks: per-cache mutex for slab list access, pool_mutex for new slab allocation.
int SlabAllocator::allocate(size_t mem_size, Alloc_Algo) {
    if (mem_size == 0) return -1;

    int cache_idx = find_cache_index(mem_size);
    if (cache_idx == -1) {
        std::cout << "[Slab] Error: Size " << mem_size
                  << " exceeds max cache size (" << CACHE_SIZES[NUM_CACHES - 1] << ").\n";
        return -1;
    }

    SlabCache& cache = caches[cache_idx];
    std::lock_guard<std::mutex> cache_lock(cache.cache_mutex);

    Slab* target_slab = nullptr;
    int slot = -1;

    if (!cache.partial.empty()) {
        target_slab = cache.partial.front();
        slot = target_slab->alloc_slot();

        if (target_slab->is_full()) {
            cache.partial.pop_front();
            cache.full.push_back(target_slab);
        }
    } else if (!cache.empty.empty()) {
        target_slab = cache.empty.front();
        cache.empty.pop_front();
        slot = target_slab->alloc_slot();

        if (target_slab->is_full()) {
            cache.full.push_back(target_slab);
        } else {
            cache.partial.push_back(target_slab);
        }
    } else {
        // Need to allocate a new slab from the pool
        {
            std::lock_guard<std::mutex> pool_lock(pool_mutex);
            target_slab = create_slab(cache_idx);
        }
        if (!target_slab) {
            std::cout << "[Slab] Error: Out of memory, cannot allocate new page slab.\n";
            return -1;
        }
        slot = target_slab->alloc_slot();

        if (target_slab->is_full()) {
            cache.full.push_back(target_slab);
        } else {
            cache.partial.push_back(target_slab);
        }
    }

    if (slot == -1) return -1;

    int id;
    size_t addr = target_slab->slot_address(slot);
    {
        std::lock_guard<std::mutex> id_lock(id_map_mutex);
        id = next_id++;
        id_map[id] = {static_cast<size_t>(cache_idx), target_slab, static_cast<size_t>(slot), addr};
    }
    cache.total_allocations++;

    return id;
}

// Frees the allocated block, returning its slot and transitioning slab states between full/partial/empty.
void SlabAllocator::deallocate(int block_id) {
    SlabAllocRecord rec;
    {
        std::lock_guard<std::mutex> id_lock(id_map_mutex);
        auto it = id_map.find(block_id);
        if (it == id_map.end()) {
            std::cout << "[Slab] Error: Block id=" << block_id << " not found.\n";
            return;
        }
        rec = it->second;
        id_map.erase(it);
    }

    SlabCache& cache = caches[rec.cache_index];
    std::lock_guard<std::mutex> cache_lock(cache.cache_mutex);

    Slab* slab = rec.slab;
    bool was_full = slab->is_full();
    slab->free_slot(rec.slot_index);
    cache.total_frees++;

    if (was_full) {
        cache.full.remove(slab);
        if (slab->is_empty()) {
            cache.empty.push_back(slab);
        } else {
            cache.partial.push_back(slab);
        }
    } else {
        if (slab->is_empty()) {
            cache.partial.remove(slab);
            cache.empty.push_back(slab);
        }
    }
}

// Returns start address of allocated slab slot.
size_t SlabAllocator::get_address(int block_id) {
    std::lock_guard<std::mutex> id_lock(id_map_mutex);
    auto it = id_map.find(block_id);
    if (it == id_map.end()) return 0;
    return it->second.address;
}

// Visualizes each slab's page range, address, slot occupancy, and bitmap.
void SlabAllocator::display() {
    std::cout << "=== Page-Backed Slab Allocator Memory Layout ===\n";
    for (size_t i = 0; i < NUM_CACHES; i++) {
        SlabCache& cache = caches[i];
        std::lock_guard<std::mutex> cache_lock(cache.cache_mutex);
        size_t total = cache.total_slabs();
        if (total == 0) continue;

        std::cout << "\n--- Cache [" << cache.object_size << " bytes] ("
                  << cache.total_pages() << " pages total) ---\n";
        std::cout << "  Slabs: " << total
                  << " (partial=" << cache.partial.size()
                  << ", full=" << cache.full.size()
                  << ", empty=" << cache.empty.size() << ")\n";

        auto print_slab_list = [](const std::list<Slab*>& lst, const std::string& label) {
            for (auto* s : lst) {
                size_t end_p = s->start_page + s->num_pages - 1;
                std::cout << "  [" << label << "] Pages [" << s->start_page << ".." << end_p
                          << "] Addr=0x" << std::hex << std::uppercase << std::setfill('0') << std::setw(4)
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
    std::cout << "=================================================\n";
}

// Prints slab pool utilization, page consumption, and per-cache metrics.
void SlabAllocator::get_statistics() {
    size_t used_pages = next_slab_address / PAGE_SIZE;

    std::cout << "\n=== Page-Backed Slab Allocator Statistics ===\n";
    std::cout << "Total Memory Pool : " << total_size << " bytes (" << total_pages << " pages)\n";
    std::cout << "Slab Pages Used   : " << used_pages << "/" << total_pages << " pages ("
              << next_slab_address << " bytes, "
              << (total_size > 0 ? (double)next_slab_address / total_size * 100.0 : 0.0) << "%)\n";
    {
        std::lock_guard<std::mutex> id_lock(id_map_mutex);
        std::cout << "Active Allocations: " << id_map.size() << "\n\n";
    }

    size_t total_used_mem = 0;

    std::cout << std::left
              << std::setw(10) << "Cache"
              << std::setw(8)  << "Pages"
              << std::setw(8)  << "Slabs"
              << std::setw(12) << "Used/Total"
              << std::setw(10) << "Allocs"
              << std::setw(8)  << "Frees"
              << std::setw(14) << "Int. Frag"
              << "\n";
    std::cout << std::string(70, '-') << "\n";

    for (size_t i = 0; i < NUM_CACHES; i++) {
        SlabCache& cache = caches[i];
        std::lock_guard<std::mutex> cache_lock(cache.cache_mutex);
        size_t total = cache.total_slabs();
        if (total == 0 && cache.total_allocations == 0) continue;

        size_t used = cache.total_used_slots();
        size_t cap = cache.total_capacity_slots();
        size_t used_bytes = used * cache.object_size;
        total_used_mem += used_bytes;

        std::cout << std::left
                  << std::setw(10) << (std::to_string(cache.object_size) + "B")
                  << std::setw(8)  << cache.total_pages()
                  << std::setw(8)  << total
                  << std::setw(12) << (std::to_string(used) + "/" + std::to_string(cap))
                  << std::setw(10) << cache.total_allocations
                  << std::setw(8)  << cache.total_frees
                  << std::setw(14) << (used_bytes > 0 ? std::to_string(used_bytes) + "B" : "-")
                  << "\n";
    }

    size_t slab_waste = next_slab_address - total_used_mem;
    double util = next_slab_address > 0 ? (double)total_used_mem / next_slab_address * 100.0 : 0.0;

    std::cout << std::string(70, '-') << "\n";
    std::cout << "Slab Space Utilization: " << std::fixed << std::setprecision(1) << util << "%\n";
    std::cout << "Slab Internal Waste   : " << slab_waste << " bytes (unused slots across allocated slab pages)\n";
    std::cout << "=============================================\n";
}
