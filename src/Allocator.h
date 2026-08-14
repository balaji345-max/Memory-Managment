#pragma once
#include <iostream>
#include <vector>
#include <unordered_map>
#include <algorithm>

enum Alloc_Algo { Firstfit, Bestfit, Worstfit };

class Allocator {
public:
    // Resets all state and creates a single free block of mem_size bytes.
    virtual void init(size_t mem_size) = 0;

    // Allocates mem_size bytes using the given strategy. Returns a unique block ID, or -1 on failure.
    virtual int allocate(size_t mem_size, Alloc_Algo algo) = 0;

    // Frees the block identified by block_id, merging adjacent free blocks if applicable.
    virtual void deallocate(int block_id) = 0;

    // Returns the start address of the block identified by block_id.
    virtual size_t get_address(int block_id) = 0;

    // Prints the current memory layout (block addresses, sizes, free/used status).
    virtual void display() = 0;

    // Prints allocation metrics (utilization, fragmentation, success rate).
    virtual void get_statistics() = 0;

    virtual ~Allocator() {};
};