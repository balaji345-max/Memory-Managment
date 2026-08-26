#include "MMap.h"
#include "Allocator.h"
#include <iostream>
#include <iomanip>
#include <algorithm>

// ---------- constructor ---------------------------------------

MMapManager::MMapManager(u64 base, u64 limit)
    : mmap_base(base), next_addr(base), max_addr(limit) {}

// ---------- do_mmap -------------------------------------------

u64 MMapManager::do_mmap(u64 addr, u64 length, int prot, int flags,
                          const std::string& filename, u64 offset) {
    if (length == 0) {
        std::cout << "[mmap] Error: length cannot be zero.\n";
        return static_cast<u64>(-1);
    }

    // Page-align the length upward
    u64 aligned_len = ((length + PAGE_SIZE - 1) / PAGE_SIZE) * PAGE_SIZE;

    u64 start = 0;

    if (flags & MM_MAP_FIXED) {
        // Use the requested address verbatim
        if (addr + aligned_len > max_addr) {
            std::cout << "[mmap] Error: MAP_FIXED region exceeds address space.\n";
            return static_cast<u64>(-1);
        }
        // Check for overlaps with existing regions
        for (auto& r : regions) {
            if (!r.active) continue;
            if (addr < r.start_addr + r.length && addr + aligned_len > r.start_addr) {
                std::cout << "[mmap] Error: MAP_FIXED overlaps existing region.\n";
                return static_cast<u64>(-1);
            }
        }
        start = addr;
    } else {
        // Auto-allocate from next_addr
        if (next_addr + aligned_len > max_addr) {
            std::cout << "[mmap] Error: no address space remaining.\n";
            return static_cast<u64>(-1);
        }
        start = next_addr;
        next_addr += aligned_len;
    }

    // Validate file-backed mapping
    if (!(flags & MM_MAP_ANONYMOUS) && !filename.empty()) {
        bool found = false;
        for (auto& f : files) {
            if (f.name == filename) { found = true; break; }
        }
        if (!found) {
            std::cout << "[mmap] Warning: file '" << filename
                      << "' not found in simulated store. Treating as anonymous.\n";
        }
    }

    MMapRegion region(start, aligned_len, prot,
                      filename.empty() ? (flags | MM_MAP_ANONYMOUS) : flags,
                      filename, offset);
    regions.push_back(region);

    total_mmaps++;
    total_mapped_bytes += aligned_len;

    return start;
}

// ---------- do_munmap -----------------------------------------

int MMapManager::do_munmap(u64 addr) {
    for (auto& r : regions) {
        if (r.active && r.start_addr == addr) {
            r.active = false;
            total_munmaps++;
            total_unmapped_bytes += r.length;
            return 0;
        }
    }
    std::cout << "[munmap] Error: no mapping at address 0x"
              << std::hex << addr << std::dec << ".\n";
    return -1;
}

// ---------- do_mprotect ---------------------------------------

int MMapManager::do_mprotect(u64 addr, int new_prot) {
    for (auto& r : regions) {
        if (r.active && addr >= r.start_addr && addr < r.start_addr + r.length) {
            r.prot = new_prot;
            total_mprotects++;
            return 0;
        }
    }
    std::cout << "[mprotect] Error: address 0x" << std::hex << addr
              << std::dec << " not in any mapped region.\n";
    return -1;
}

// ---------- queries -------------------------------------------

bool MMapManager::contains(u64 addr) const {
    for (auto& r : regions) {
        if (r.active && addr >= r.start_addr && addr < r.start_addr + r.length)
            return true;
    }
    return false;
}

bool MMapManager::check_access(u64 addr, bool is_write) const {
    for (auto& r : regions) {
        if (!r.active) continue;
        if (addr >= r.start_addr && addr < r.start_addr + r.length) {
            if (is_write && !(r.prot & MM_PROT_WRITE)) return false;
            if (!is_write && !(r.prot & MM_PROT_READ))  return false;
            return true;
        }
    }
    // Address not in any mmap region — allow (normal VM handles it)
    return true;
}

const MMapRegion* MMapManager::find_region(u64 addr) const {
    for (auto& r : regions) {
        if (r.active && addr >= r.start_addr && addr < r.start_addr + r.length)
            return &r;
    }
    return nullptr;
}

bool MMapManager::is_file_backed(u64 addr) const {
    auto* r = find_region(addr);
    if (!r) return false;
    return !(r->flags & MM_MAP_ANONYMOUS) && !r->filename.empty();
}

// ---------- simulated file API --------------------------------

void MMapManager::create_file(const std::string& name, size_t size) {
    // Overwrite if exists
    for (auto& f : files) {
        if (f.name == name) {
            f.data.assign(size, 0xAB);   // fill with pattern
            std::cout << "[mmap] File '" << name << "' resized to " << size << " bytes.\n";
            return;
        }
    }
    SimFile sf;
    sf.name = name;
    sf.data.assign(size, 0xAB);
    files.push_back(std::move(sf));
    std::cout << "[mmap] File '" << name << "' created (" << size << " bytes).\n";
}

// ---------- display -------------------------------------------

static std::string prot_str(int prot) {
    std::string s;
    s += (prot & MM_PROT_READ)  ? 'r' : '-';
    s += (prot & MM_PROT_WRITE) ? 'w' : '-';
    s += (prot & MM_PROT_EXEC)  ? 'x' : '-';
    return s;
}

static std::string flags_str(int flags) {
    std::string s;
    if (flags & MM_MAP_PRIVATE)   s += "PRIVATE ";
    if (flags & MM_MAP_SHARED)    s += "SHARED ";
    if (flags & MM_MAP_ANONYMOUS) s += "ANON ";
    if (flags & MM_MAP_FIXED)     s += "FIXED ";
    if (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

void MMapManager::display_mappings() const {
    std::cout << "\n=== mmap Region Map ===\n";
    std::cout << std::left
              << std::setw(14) << "Start"
              << std::setw(14) << "End"
              << std::setw(10) << "Length"
              << std::setw(6)  << "Prot"
              << std::setw(16) << "Flags"
              << "Backing\n";
    std::cout << std::string(70, '-') << "\n";

    for (auto& r : regions) {
        if (!r.active) continue;
        std::cout << "0x" << std::hex << std::setfill('0') << std::setw(8)
                  << r.start_addr << "  "
                  << "0x" << std::setw(8) << (r.start_addr + r.length - 1) << "  "
                  << std::dec << std::setfill(' ')
                  << std::setw(8) << r.length << "  "
                  << std::setw(4) << prot_str(r.prot) << "  "
                  << std::setw(14) << flags_str(r.flags) << "  "
                  << (r.filename.empty() ? "[anonymous]" : r.filename) << "\n";
    }
    std::cout << "========================\n";
}

void MMapManager::get_statistics() const {
    u64 active_regions = 0, active_bytes = 0;
    for (auto& r : regions) {
        if (r.active) { active_regions++; active_bytes += r.length; }
    }

    std::cout << "\n--- mmap Statistics ---\n";
    std::cout << "Total mmap calls   : " << total_mmaps << "\n";
    std::cout << "Total munmap calls : " << total_munmaps << "\n";
    std::cout << "Total mprotect     : " << total_mprotects << "\n";
    std::cout << "Active regions     : " << active_regions << "\n";
    std::cout << "Active mapped bytes: " << active_bytes << "\n";
    std::cout << "Total mapped       : " << total_mapped_bytes << " bytes\n";
    std::cout << "Total unmapped     : " << total_unmapped_bytes << " bytes\n";
    std::cout << "----------------------\n";
}
