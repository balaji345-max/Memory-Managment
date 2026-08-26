#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include <iostream>

using u64 = uint64_t;

// Protection flags (bitwise-OR combinable, mirroring POSIX mmap)
constexpr int MM_PROT_NONE  = 0x0;
constexpr int MM_PROT_READ  = 0x1;
constexpr int MM_PROT_WRITE = 0x2;
constexpr int MM_PROT_EXEC  = 0x4;

// Mapping flags
constexpr int MM_MAP_PRIVATE   = 0x1;
constexpr int MM_MAP_SHARED    = 0x2;
constexpr int MM_MAP_ANONYMOUS = 0x4;
constexpr int MM_MAP_FIXED     = 0x8;

// Forward declarations — avoids circular includes with VirtualMemory
class VirtualMemory;
class TLB;

// Represents a contiguous virtual-address region created by mmap.
struct MMapRegion {
    u64         start_addr;
    u64         length;
    int         prot;           // MM_PROT_*
    int         flags;          // MM_MAP_*
    std::string filename;       // empty for anonymous mappings
    u64         file_offset;
    bool        active;

    MMapRegion()
        : start_addr(0), length(0), prot(0), flags(0),
          file_offset(0), active(false) {}

    MMapRegion(u64 addr, u64 len, int p, int f,
               const std::string& fname = "", u64 foff = 0)
        : start_addr(addr), length(len), prot(p), flags(f),
          filename(fname), file_offset(foff), active(true) {}
};

// Simulates the mmap / munmap / mprotect system-call interface.
//
// Region addresses are allocated from a configurable mmap-base that sits
// above the process heap, mirroring how Linux separates the two.
class MMapManager {
private:
    std::vector<MMapRegion> regions;
    u64 mmap_base;          // first auto-assigned address
    u64 next_addr;          // next free auto-assign address
    u64 max_addr;           // upper bound of virtual address space

    // Statistics
    u64 total_mmaps     = 0;
    u64 total_munmaps   = 0;
    u64 total_mprotects = 0;
    u64 total_mapped_bytes   = 0;
    u64 total_unmapped_bytes = 0;

    // Simulated "file store" for file-backed mappings
    struct SimFile {
        std::string name;
        std::vector<uint8_t> data;
    };
    std::vector<SimFile> files;

public:
    // base  = lowest address that mmap can hand out
    // limit = upper bound of the virtual address space
    MMapManager(u64 base, u64 limit);

    // ---- core syscalls ----

    // Allocates a region of `length` bytes with the given protection.
    // If MM_MAP_FIXED is set, `addr` is used verbatim (may overlap/fail).
    // Returns the start address of the new mapping, or (u64)-1 on failure.
    u64  do_mmap(u64 addr, u64 length, int prot, int flags,
                 const std::string& filename = "", u64 offset = 0);

    // Unmaps the region starting at `addr`.  Returns 0 on success, -1 on error.
    int  do_munmap(u64 addr);

    // Changes protection for the region containing `addr`.  Returns 0 / -1.
    int  do_mprotect(u64 addr, int new_prot);

    // ---- queries ----

    // Returns true if `addr` falls inside an active region.
    bool contains(u64 addr) const;

    // Returns true if the access type (read/write) is allowed by the
    // region's protection bits.  If the address is not in any region the
    // access is considered valid (falls through to normal VM handling).
    bool check_access(u64 addr, bool is_write) const;

    // Returns the MMapRegion for `addr`, or nullptr.
    const MMapRegion* find_region(u64 addr) const;

    // Is this address in a file-backed mapping?
    bool is_file_backed(u64 addr) const;

    // ---- simulated file API ----

    void create_file(const std::string& name, size_t size);

    // ---- display ----

    void display_mappings() const;
    void get_statistics()   const;

    // Accessors
    const std::vector<MMapRegion>& get_regions() const { return regions; }
};
