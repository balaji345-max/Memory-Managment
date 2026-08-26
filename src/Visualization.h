#pragma once
#include <string>
#include <iostream>
#include "MemoryAllocator.h"
#include "BuddyAllocator.h"
#include "SlabAllocator.h"
#include "Cache.h"
#include "VirtualMemory.h"
#include "MMap.h"

// ANSI color codes for terminal rendering
namespace Color {
    const std::string RESET   = "\033[0m";
    const std::string BOLD    = "\033[1m";
    const std::string RED     = "\033[31m";
    const std::string GREEN   = "\033[32m";
    const std::string YELLOW  = "\033[33m";
    const std::string BLUE    = "\033[34m";
    const std::string MAGENTA = "\033[35m";
    const std::string CYAN    = "\033[36m";
    const std::string WHITE   = "\033[37m";
    const std::string BG_RED    = "\033[41m";
    const std::string BG_GREEN  = "\033[42m";
    const std::string BG_YELLOW = "\033[43m";
    const std::string BG_BLUE   = "\033[44m";
    const std::string BG_MAGENTA= "\033[45m";
    const std::string BG_CYAN   = "\033[46m";
    const std::string GRAY    = "\033[90m";
}

// Terminal-based ANSI-color visualization for memory layout,
// fragmentation metrics, cache activity, and page mappings.
class MemoryVisualizer {
public:
    // Render the linear allocator's memory map as colored blocks (1 char = 1 page)
    static void render_linear(const MemoryAllocator* alloc);

    // Render the buddy allocator's free-list tree structure
    static void render_buddy(const BuddyAllocator* alloc);

    // Render slab allocator's per-cache bitmap grids
    static void render_slab(const SlabAllocator* alloc);

    // Render fragmentation gauge bars for any allocator
    static void render_fragmentation(const MemoryAllocator* alloc);

    // Render cache hierarchy heatmap showing per-set activity
    static void render_cache_heatmap(const MemoryHierarchy* cache);

    // Render page table VPN→PFN mapping grid
    static void render_page_map(u64 num_virtual_pages, u64 num_physical_frames);

    // Render mmap region visualization
    static void render_mmap(const MMapManager* mgr);

    // Render all visualizations for current allocator type
    static void render_all(Allocator* alloc, MemoryHierarchy* cache, MMapManager* mgr);

private:
    // Helper: draw a horizontal gauge bar
    static void draw_gauge(const std::string& label, double fraction,
                           int width, const std::string& fill_color,
                           const std::string& empty_color);
};
