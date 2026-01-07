#include "BuddyAllocator.h"
#include <iostream>
#include <algorithm>
#include <climits>

BuddyAllocator::~BuddyAllocator() {
    for (auto& entry : allocated) {
        delete entry.second;
    }
    allocated.clear();

    for (auto head : free_lists) {
        while (head) {
            BuddyBlock* temp = head;
            head = head->next;
            delete temp;
        }
    }
    free_lists.clear();
}

int BuddyAllocator::order_of(size_t x) {
    if (x == 0 || (x & (x - 1)) != 0) return -1;

    int order = 0;
    while (x > 1) {
        x >>= 1;
        order++;
    }
    return order;
}

size_t BuddyAllocator::next_power_of_2(size_t x) {
    if (x == 0) return 1;

    constexpr size_t MAX_POW2 = size_t(1) << (sizeof(size_t) * 8 - 1);
    if (x > MAX_POW2) return 0;

    if ((x & (x - 1)) == 0) return x;

    x--;
    x |= x >> 1;
    x |= x >> 2;
    x |= x >> 4;
    x |= x >> 8;
    x |= x >> 16;
    x |= x >> 32;

    return x + 1;
}

void BuddyAllocator::init(size_t size) {
    for (auto& entry : allocated) {
        delete entry.second;
    }
    allocated.clear();

    for (auto head : free_lists) {
        while (head) {
            BuddyBlock* temp = head;
            head = head->next;
            delete temp;
        }
    }

    total_size = next_power_of_2(size);
    if (total_size == 0) {
        std::cerr << "[Error] Requested size too large.\n";
        return;
    }

    int max_order = order_of(total_size);
    free_lists.assign(max_order + 1, nullptr);
    next_id = 1;

    free_lists[max_order] = new BuddyBlock(0, total_size);

    std::cout << "[System] Buddy Memory Initialized: "
              << total_size << " bytes (Order " << max_order << ").\n";
}

int BuddyAllocator::allocate(size_t size, Alloc_Algo) {
    if (size == 0) return -1;

    size_t req_size = next_power_of_2(size);
    if (req_size == 0 || req_size > total_size) return -1;

    int req_order = order_of(req_size);
    if (req_order == -1) return -1;

    int current_order = req_order;
    int max_order = static_cast<int>(free_lists.size()) - 1;

    while (current_order <= max_order && free_lists[current_order] == nullptr) {
        current_order++;
    }
    if (current_order > max_order) return -1;

    BuddyBlock* blk = free_lists[current_order];
    free_lists[current_order] = blk->next;
    blk->next = nullptr;

    while (current_order > req_order) {
        current_order--;
        size_t half = blk->size / 2;

        BuddyBlock* buddy = new BuddyBlock(blk->address + half, half);
        blk->size = half;

        buddy->next = free_lists[current_order];
        free_lists[current_order] = buddy;
    }

    blk->id = next_id++;
    allocated[blk->id] = blk;
    return blk->id;
}

void BuddyAllocator::deallocate(int id) {
    auto it = allocated.find(id);
    if (it == allocated.end()) return;

    BuddyBlock* blk = it->second;
    size_t addr = blk->address;
    size_t size = blk->size;

    allocated.erase(it);
    delete blk;

    while (size < total_size) {
        size_t buddy_addr = addr ^ size;
        int order = order_of(size);

        if (order == -1 || order >= static_cast<int>(free_lists.size()) - 1) break;

        BuddyBlock* prev = nullptr;
        BuddyBlock* curr = free_lists[order];

        while (curr && curr->address != buddy_addr) {
            prev = curr;
            curr = curr->next;
        }
        if (!curr) break;

        if (prev) prev->next = curr->next;
        else free_lists[order] = curr->next;

        delete curr;

        addr = std::min(addr, buddy_addr);
        size <<= 1;
    }

    int final_order = order_of(size);
    if (final_order != -1 && final_order < static_cast<int>(free_lists.size())) {
        BuddyBlock* merged = new BuddyBlock(addr, size);
        merged->next = free_lists[final_order];
        free_lists[final_order] = merged;
    }
}

size_t BuddyAllocator::get_address(int id) {
    auto it = allocated.find(id);
    if (it == allocated.end()) return SIZE_MAX;
    return it->second->address;
}

void BuddyAllocator::display() {
    std::cout << "--- Free Lists ---\n";
    for (size_t i = 0; i < free_lists.size(); i++) {
        std::cout << "Order " << i << " (" << (1ULL << i) << "): ";
        BuddyBlock* curr = free_lists[i];
        while (curr) {
            std::cout << "[Addr:" << curr->address
                      << ", Size:" << curr->size << "] -> ";
            curr = curr->next;
        }
        std::cout << "nullptr\n";
    }
}

void BuddyAllocator::get_statistics() {
    size_t free_mem = 0, free_blocks = 0;

    for (auto head : free_lists) {
        while (head) {
            free_mem += head->size;
            free_blocks++;
            head = head->next;
        }
    }

    std::cout << "Total Memory      : " << total_size << "\n";
    std::cout << "Allocated Blocks  : " << allocated.size() << "\n";
    std::cout << "Free Blocks       : " << free_blocks << "\n";
    std::cout << "Free Memory       : " << free_mem << "\n";
    std::cout << "Used Memory       : " << (total_size - free_mem) << "\n";
}
