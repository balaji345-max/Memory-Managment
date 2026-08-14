#pragma once
#include <vector>
#include <iostream>
#include <unordered_map>
#include <algorithm>
#include <cmath>
#include "Allocator.h"

// Represents a node in the buddy system free lists.
struct BuddyBlock {
    size_t address;
    size_t size;
    int id;
    BuddyBlock* next;
    BuddyBlock(size_t addr, size_t s) : address(addr), size(s), id(0), next(nullptr) {}
};

// Binary Buddy Memory Allocator.
// Allocations are rounded up to powers of 2.
// Manages power-of-2 free lists by order and recursively splits/merges buddy blocks.
class BuddyAllocator : public Allocator {
private:
    size_t total_size{};
    int next_id{1};
    std::vector<BuddyBlock*> free_lists;
    std::unordered_map<int, BuddyBlock*> allocated;
    
    // Computes the smallest power of 2 greater than or equal to x.
    size_t next_power_of_2(size_t x);

    // Returns log2(x) if x is a power of 2, or -1 otherwise.
    int order_of(size_t x);

public:
    BuddyAllocator() = default;
    ~BuddyAllocator() override;

    // Resets buddy state and initializes memory pool rounded up to power of 2.
    void init(size_t size) override;

    // Allocates memory rounded to power-of-2 size by finding/splitting order blocks.
    int allocate(size_t size, Alloc_Algo algo = Firstfit) override;

    // Frees block by id and recursively merges with its buddy block if also free.
    void deallocate(int id) override;

    // Returns start address of allocated block.
    size_t get_address(int Id) override;

    // Prints free list chains across all order levels.
    void display() override;

    // Prints buddy memory usage and free block statistics.
    void get_statistics() override;
};