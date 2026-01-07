#pragma once
#include "Allocator.h"

struct Mem_Block {
    int Id;
    size_t start_address;
    size_t mem_size;      
    size_t req_size;      
    bool is_free;
    Mem_Block* next;
    Mem_Block* prev;

    Mem_Block(size_t addr, size_t sz, int id = 0, bool free = true, size_t req = 0)
        : Id(id), start_address(addr), mem_size(sz), req_size(req), is_free(free), next(nullptr), prev(nullptr) {}
};

class MemoryAllocator : public Allocator {
private:
    size_t total_size;
    Mem_Block* head;
    int next_Id;
    std::unordered_map<int, Mem_Block*> id_map;
    size_t total_alloc_attempts = 0;
    size_t successful_allocations = 0;

public:
    MemoryAllocator();
    ~MemoryAllocator();
    void init(size_t mem_size) override;
    int allocate(size_t mem_size, Alloc_Algo algo) override;
    void deallocate(int Id) override;
    size_t get_address(int Id) override;
    void display() override;
    void get_statistics() override;
};