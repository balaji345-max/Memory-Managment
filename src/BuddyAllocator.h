#pragma once
#include <vector>
#include <iostream>
#include <unordered_map>
#include <algorithm>
#include <cmath>
#include <mutex>
#include "Allocator.h"

// Represents a node in the page-level buddy system free lists.
struct BuddyBlock {
    size_t address;
    size_t size;        // Size in bytes
    size_t page_count;  // Size in 64-byte pages
    int id;
    BuddyBlock* next;

    BuddyBlock(size_t addr, size_t s)
        : address(addr), size(s), page_count(s / PAGE_SIZE), id(0), next(nullptr) {}
};

// Page-Level Binary Buddy Allocator (modeling Linux kernel alloc_pages).
// Orders represent powers of 2 in physical/virtual page units (Order 0 = 1 Page = 64B, Order 1 = 2 Pages, etc.).
// Recursively splits and merges buddy page blocks.
// Thread-safe: a single mutex guards allocation and deallocation.
class BuddyAllocator : public Allocator {
private:
    size_t total_size{};
    size_t total_pages{};
    int next_id{1};
    std::vector<BuddyBlock*> free_lists;
    std::unordered_map<int, BuddyBlock*> allocated;
    
    mutable std::mutex alloc_mutex;  // guards free_lists + allocated map

    // Calculates power-of-2 page order for a given byte size (Order 0 = 64B = 1 Page).
    int order_of(size_t bytes);

    // Rounds requested size to nearest power-of-2 page count in bytes.
    size_t round_to_buddy_size(size_t bytes);

public:
    BuddyAllocator() = default;
    ~BuddyAllocator() override;

    // Resets buddy state and initializes memory pool rounded up to power-of-2 pages.
    void init(size_t size) override;

    // Allocates memory in units of 2^k pages by finding/splitting order blocks.
    int allocate(size_t size, Alloc_Algo algo = Firstfit) override;

    // Frees block by id and recursively merges with its buddy page block if also free.
    void deallocate(int id) override;

    // Returns start address of allocated block.
    size_t get_address(int Id) override;

    // Prints free list chains across all page order levels.
    void display() override;

    // Prints page-level memory metrics, allocated pages, and free memory breakdown.
    void get_statistics() override;

    // Accessors for visualization and benchmarking
    const std::vector<BuddyBlock*>& get_free_lists()  const { return free_lists; }
    size_t get_total_size_val()  const { return total_size; }
    size_t get_total_pages_val() const { return total_pages; }
    const std::unordered_map<int, BuddyBlock*>& get_allocated() const { return allocated; }
};