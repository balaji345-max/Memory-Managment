#include "Visualization.h"
#include <iomanip>
#include <sstream>
#include <cmath>
#include <algorithm>

// ==================== Helper: gauge bar =======================

void MemoryVisualizer::draw_gauge(const std::string& label, double fraction,
                                   int width, const std::string& fill_color,
                                   const std::string& empty_color) {
    int filled = static_cast<int>(fraction * width);
    if (filled > width) filled = width;
    if (filled < 0) filled = 0;

    std::cout << "  " << std::left << std::setw(24) << label << " [";
    for (int i = 0; i < width; i++) {
        if (i < filled)
            std::cout << fill_color << "\u2588" << Color::RESET;
        else
            std::cout << empty_color << "\u2591" << Color::RESET;
    }
    std::cout << "] " << std::fixed << std::setprecision(1) << (fraction * 100.0) << "%\n";
}

// ==================== Linear Allocator ========================

void MemoryVisualizer::render_linear(const MemoryAllocator* alloc) {
    if (!alloc || !alloc->get_head()) return;

    size_t total = alloc->get_total_size();
    if (total == 0) return;

    std::cout << "\n" << Color::BOLD << Color::CYAN
              << "  ╔══════════════════════════════════════════════╗\n"
              << "  ║      Linear Allocator Memory Map             ║\n"
              << "  ╚══════════════════════════════════════════════╝" << Color::RESET << "\n\n";

    // Memory map: each character represents one page (PAGE_SIZE bytes)
    size_t total_pages = total / PAGE_SIZE;
    if (total_pages == 0) total_pages = 1;

    // Build page status array
    std::vector<int> page_map(total_pages, 0);  // 0 = free, block_id for used

    Mem_Block* curr = alloc->get_head();
    while (curr) {
        if (!curr->is_free) {
            size_t start_page = curr->start_address / PAGE_SIZE;
            size_t end_page = (curr->start_address + curr->mem_size - 1) / PAGE_SIZE;
            for (size_t p = start_page; p <= end_page && p < total_pages; p++) {
                page_map[p] = curr->Id;
            }
        }
        curr = curr->next;
    }

    // Render map (max 64 chars per row)
    int cols = std::min<int>(64, static_cast<int>(total_pages));
    std::cout << "  " << Color::GRAY << "Memory Map (1 cell = " << PAGE_SIZE << "B page):" << Color::RESET << "\n  ";
    for (size_t i = 0; i < total_pages; i++) {
        if (i > 0 && i % cols == 0) std::cout << "\n  ";
        if (page_map[i] > 0) {
            // Cycle through colors for different blocks
            int color_idx = page_map[i] % 6;
            std::string colors[] = {Color::BG_RED, Color::BG_BLUE, Color::BG_MAGENTA,
                                     Color::BG_CYAN, Color::BG_YELLOW, Color::BG_GREEN};
            std::cout << colors[color_idx] << Color::WHITE << std::setw(1) << (page_map[i] % 10) << Color::RESET;
        } else {
            std::cout << Color::GREEN << "\u2591" << Color::RESET;
        }
    }
    std::cout << "\n\n";

    // Legend
    std::cout << "  " << Color::GREEN << "\u2591" << Color::RESET << " = FREE    ";
    std::cout << Color::BG_RED << Color::WHITE << " " << Color::RESET << " = USED (id shown)\n\n";

    // Block list
    std::cout << "  " << Color::BOLD << "Block Detail:" << Color::RESET << "\n";
    curr = alloc->get_head();
    while (curr) {
        std::string status = curr->is_free ? (Color::GREEN + "FREE" + Color::RESET) 
                                           : (Color::RED + "USED id=" + std::to_string(curr->Id) + Color::RESET);
        std::cout << "  [0x" << std::hex << std::setfill('0') << std::setw(4) << curr->start_address
                  << " - 0x" << std::setw(4) << (curr->start_address + curr->mem_size - 1) << "] "
                  << std::dec << std::setfill(' ') << std::setw(6) << curr->mem_size << "B  " << status << "\n";
        curr = curr->next;
    }
    std::cout << "\n";
}

// ==================== Fragmentation ==========================

void MemoryVisualizer::render_fragmentation(const MemoryAllocator* alloc) {
    if (!alloc || !alloc->get_head()) return;

    std::cout << "\n" << Color::BOLD << Color::YELLOW
              << "  ╔══════════════════════════════════════════════╗\n"
              << "  ║       Fragmentation Analysis                 ║\n"
              << "  ╚══════════════════════════════════════════════╝" << Color::RESET << "\n\n";

    size_t total_free = 0, used = 0, internal_frag = 0;
    size_t largest_free = 0, free_blocks = 0;
    size_t total = alloc->get_total_size();

    Mem_Block* curr = alloc->get_head();
    while (curr) {
        if (curr->is_free) {
            total_free += curr->mem_size;
            if (curr->mem_size > largest_free) largest_free = curr->mem_size;
            free_blocks++;
        } else {
            used += curr->mem_size;
            internal_frag += (curr->mem_size - curr->req_size);
        }
        curr = curr->next;
    }

    double utilization = total > 0 ? (double)used / total : 0.0;
    double ext_frag = total_free > 0 ? (double)(total_free - largest_free) / total_free : 0.0;
    double int_frag = used > 0 ? (double)internal_frag / used : 0.0;

    draw_gauge("Memory Utilization", utilization, 40, Color::CYAN, Color::GRAY);
    draw_gauge("External Fragmentation", ext_frag, 40, Color::RED, Color::GRAY);
    draw_gauge("Internal Fragmentation", int_frag, 40, Color::YELLOW, Color::GRAY);

    std::cout << "\n  " << Color::BOLD << "Details:" << Color::RESET << "\n";
    std::cout << "  Total: " << total << "B | Used: " << used << "B | Free: " << total_free << "B\n";
    std::cout << "  Free blocks: " << free_blocks << " | Largest free: " << largest_free << "B\n";
    std::cout << "  Internal waste: " << internal_frag << "B\n\n";
}

// ==================== Buddy Allocator =========================

void MemoryVisualizer::render_buddy(const BuddyAllocator* alloc) {
    if (!alloc) return;

    std::cout << "\n" << Color::BOLD << Color::MAGENTA
              << "  ╔══════════════════════════════════════════════╗\n"
              << "  ║      Buddy Allocator Free List Tree          ║\n"
              << "  ╚══════════════════════════════════════════════╝" << Color::RESET << "\n\n";

    auto& free_lists = alloc->get_free_lists();
    size_t total = alloc->get_total_size_val();
    size_t total_pages = alloc->get_total_pages_val();

    // Build page-level allocation map
    std::vector<bool> page_used(total_pages, true); // assume used
    for (size_t order = 0; order < free_lists.size(); order++) {
        BuddyBlock* curr = free_lists[order];
        while (curr) {
            size_t start_p = curr->address / PAGE_SIZE;
            size_t count = curr->page_count;
            for (size_t p = start_p; p < start_p + count && p < total_pages; p++) {
                page_used[p] = false;
            }
            curr = curr->next;
        }
    }

    // Render page map
    int cols = std::min<int>(64, static_cast<int>(total_pages));
    std::cout << "  " << Color::GRAY << "Page Map (1 cell = 1 page = " << PAGE_SIZE << "B):" << Color::RESET << "\n  ";
    for (size_t i = 0; i < total_pages; i++) {
        if (i > 0 && i % cols == 0) std::cout << "\n  ";
        if (page_used[i])
            std::cout << Color::BG_RED << " " << Color::RESET;
        else
            std::cout << Color::BG_GREEN << " " << Color::RESET;
    }
    std::cout << "\n\n";

    // Free list summary per order
    std::cout << "  " << Color::BOLD << "Free List by Order:" << Color::RESET << "\n";
    for (size_t i = 0; i < free_lists.size(); i++) {
        size_t pages = 1ULL << i;
        size_t bytes = pages * PAGE_SIZE;
        int count = 0;
        BuddyBlock* curr = free_lists[i];
        while (curr) { count++; curr = curr->next; }

        std::cout << "  Order " << i << " (" << std::setw(4) << pages << " pg = "
                  << std::setw(6) << bytes << "B): ";
        if (count > 0) {
            for (int j = 0; j < count && j < 20; j++)
                std::cout << Color::GREEN << "\u2588" << Color::RESET;
            std::cout << " " << count << " block(s)";
        } else {
            std::cout << Color::GRAY << "empty" << Color::RESET;
        }
        std::cout << "\n";
    }

    // Utilization
    size_t alloc_count = alloc->get_allocated().size();
    size_t free_mem = 0;
    for (size_t order = 0; order < free_lists.size(); order++) {
        BuddyBlock* curr = free_lists[order];
        while (curr) { free_mem += curr->size; curr = curr->next; }
    }
    double util = total > 0 ? (double)(total - free_mem) / total : 0.0;
    std::cout << "\n";
    draw_gauge("Memory Utilization", util, 40, Color::CYAN, Color::GRAY);
    std::cout << "  Active allocations: " << alloc_count << "\n\n";
}

// ==================== Slab Allocator ==========================

void MemoryVisualizer::render_slab(const SlabAllocator* alloc) {
    if (!alloc) return;

    std::cout << "\n" << Color::BOLD << Color::BLUE
              << "  ╔══════════════════════════════════════════════╗\n"
              << "  ║      Slab Allocator Cache Bitmaps            ║\n"
              << "  ╚══════════════════════════════════════════════╝" << Color::RESET << "\n\n";

    const SlabCache* caches = alloc->get_caches();
    for (size_t i = 0; i < alloc->get_num_caches(); i++) {
        const SlabCache& cache = caches[i];
        size_t total_slabs = cache.partial.size() + cache.full.size() + cache.empty.size();
        if (total_slabs == 0) continue;

        size_t used = cache.total_used_slots();
        size_t cap = cache.total_capacity_slots();
        double util = cap > 0 ? (double)used / cap : 0.0;

        std::cout << "  " << Color::BOLD << "Cache [" << cache.object_size << "B]" << Color::RESET
                  << " — " << total_slabs << " slab(s), " << used << "/" << cap << " slots\n";

        // Bitmap for each slab
        auto render_slabs = [](const std::list<Slab*>& lst, const std::string& label, const std::string& color) {
            for (auto* s : lst) {
                std::cout << "    " << color << "[" << label << "]" << Color::RESET << " |";
                for (size_t j = 0; j < s->num_slots; j++) {
                    if (s->bitmap[j])
                        std::cout << Color::RED << "\u2588" << Color::RESET;
                    else
                        std::cout << Color::GREEN << "\u2591" << Color::RESET;
                }
                std::cout << "| " << s->num_used << "/" << s->num_slots << "\n";
            }
        };

        render_slabs(cache.partial, "P", Color::YELLOW);
        render_slabs(cache.full,    "F", Color::RED);
        render_slabs(cache.empty,   "E", Color::GREEN);

        draw_gauge("  Utilization", util, 30, Color::CYAN, Color::GRAY);
        std::cout << "\n";
    }
}

// ==================== Cache Heatmap ===========================

void MemoryVisualizer::render_cache_heatmap(const MemoryHierarchy* cache) {
    if (!cache) return;

    std::cout << "\n" << Color::BOLD << Color::RED
              << "  ╔══════════════════════════════════════════════╗\n"
              << "  ║       Cache Hierarchy Heatmap                ║\n"
              << "  ╚══════════════════════════════════════════════╝" << Color::RESET << "\n\n";

    // Need non-const access to get stats - cast away const for display-only
    auto render_level = [](const CacheLevel* level) {
        int id = level->get_level_id();
        u64 num_sets = level->get_num_sets();
        int assoc = level->get_associativity();
        auto& sets = level->get_sets();

        u64 total_acc = level->get_accesses();
        double hr = total_acc > 0 ? 100.0 * level->get_hits() / total_acc : 0.0;

        std::cout << "  " << Color::BOLD << "L" << id << Color::RESET
                  << " (" << level->get_size() << "B, " << num_sets << " sets × "
                  << assoc << "-way, " << level->get_block_size() << "B blocks)"
                  << "  Hit Rate: " << std::fixed << std::setprecision(1) << hr << "%\n";

        // Heatmap: show valid-line density per set
        std::cout << "  Sets: ";
        for (u64 s = 0; s < num_sets && s < 64; s++) {
            int valid_count = 0;
            for (int w = 0; w < assoc; w++) {
                if (sets[s][w].valid) valid_count++;
            }
            double density = (double)valid_count / assoc;
            if (density >= 0.75)
                std::cout << Color::BG_RED << " " << Color::RESET;
            else if (density >= 0.5)
                std::cout << Color::BG_YELLOW << " " << Color::RESET;
            else if (density > 0)
                std::cout << Color::BG_GREEN << " " << Color::RESET;
            else
                std::cout << Color::GRAY << "\u2591" << Color::RESET;
        }
        std::cout << "\n         ";
        std::cout << Color::BG_RED << " " << Color::RESET << "=hot  "
                  << Color::BG_YELLOW << " " << Color::RESET << "=warm  "
                  << Color::BG_GREEN << " " << Color::RESET << "=cool  "
                  << Color::GRAY << "\u2591" << Color::RESET << "=empty\n\n";
    };

    // We need const_cast since we're only reading, but the getters return const refs
    render_level(const_cast<MemoryHierarchy*>(cache)->get_l1());
    render_level(const_cast<MemoryHierarchy*>(cache)->get_l2());
    render_level(const_cast<MemoryHierarchy*>(cache)->get_l3());
}

// ==================== Page Map ================================

void MemoryVisualizer::render_page_map(u64 num_virtual_pages, u64 num_physical_frames) {
    std::cout << "\n" << Color::BOLD << Color::GREEN
              << "  ╔══════════════════════════════════════════════╗\n"
              << "  ║      Virtual → Physical Page Mapping         ║\n"
              << "  ╚══════════════════════════════════════════════╝" << Color::RESET << "\n\n";

    std::cout << "  Virtual pages : " << num_virtual_pages << "\n";
    std::cout << "  Physical frames: " << num_physical_frames << "\n";
    std::cout << "  (Use 'stats' for detailed mapping info)\n\n";
}

// ==================== MMap ====================================

void MemoryVisualizer::render_mmap(const MMapManager* mgr) {
    if (!mgr) return;

    std::cout << "\n" << Color::BOLD << Color::MAGENTA
              << "  ╔══════════════════════════════════════════════╗\n"
              << "  ║       mmap Region Visualization              ║\n"
              << "  ╚══════════════════════════════════════════════╝" << Color::RESET << "\n\n";

    auto& regions = mgr->get_regions();
    bool has_active = false;
    for (auto& r : regions) {
        if (!r.active) continue;
        has_active = true;

        size_t pages = r.length / PAGE_SIZE;
        std::string prot;
        prot += (r.prot & MM_PROT_READ)  ? 'r' : '-';
        prot += (r.prot & MM_PROT_WRITE) ? 'w' : '-';
        prot += (r.prot & MM_PROT_EXEC)  ? 'x' : '-';

        std::string backing = (r.flags & MM_MAP_ANONYMOUS) ? "anonymous" : r.filename;

        std::cout << "  " << Color::CYAN << "0x" << std::hex << std::setfill('0') << std::setw(8) << r.start_addr
                  << Color::RESET << " ";

        // Draw pages as blocks
        for (size_t p = 0; p < pages && p < 48; p++) {
            if (r.prot & MM_PROT_WRITE)
                std::cout << Color::BG_BLUE << " " << Color::RESET;
            else
                std::cout << Color::BG_GREEN << " " << Color::RESET;
        }
        if (pages > 48) std::cout << "...";

        std::cout << " " << std::dec << std::setfill(' ') << r.length << "B [" << prot << "] " << backing << "\n";
    }

    if (!has_active) {
        std::cout << "  " << Color::GRAY << "(no active mmap regions)" << Color::RESET << "\n";
    }
    std::cout << "\n";
}

// ==================== Render All ==============================

void MemoryVisualizer::render_all(Allocator* alloc, MemoryHierarchy* cache, MMapManager* mgr) {
    // Try each allocator type
    auto* linear = dynamic_cast<MemoryAllocator*>(alloc);
    auto* buddy = dynamic_cast<BuddyAllocator*>(alloc);
    auto* slab = dynamic_cast<SlabAllocator*>(alloc);

    if (linear) {
        render_linear(linear);
        render_fragmentation(linear);
    } else if (buddy) {
        render_buddy(buddy);
    } else if (slab) {
        render_slab(slab);
    }

    render_cache_heatmap(cache);
    render_mmap(mgr);
}
