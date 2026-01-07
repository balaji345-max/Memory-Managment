#pragma once
#include <vector>
#include <iostream>
#include <unordered_map>
#include <algorithm>
#include <cmath>
#include "Allocator.h"

struct BuddyBlock {
    size_t address;
    size_t size;
    int id;
    BuddyBlock* next;
    BuddyBlock(size_t addr, size_t s) : address(addr), size(s), id(0), next(nullptr) {}
};

class BuddyAllocator : public Allocator {
private:
    size_t total_size{};
    int next_id{1};
    std::vector<BuddyBlock*> free_lists;
    std::unordered_map<int, BuddyBlock*> allocated;
    
    size_t next_power_of_2(size_t x);
    int order_of(size_t x);

public:
    void init(size_t size) override;
    int allocate(size_t size, Alloc_Algo algo = Firstfit) override; 
    void deallocate(int id) override;
    size_t get_address(int Id) override;
    void display() override;
    void get_statistics() override;
    ~BuddyAllocator();
};