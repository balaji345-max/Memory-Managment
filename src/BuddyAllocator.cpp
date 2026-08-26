#include "BuddyAllocator.h"
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <climits>

// Releases all allocated blocks and free list nodes.
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

// Calculates power-of-two page order (Order 0 = PAGE_SIZE = 64B, Order 1 = 128B = 2 pages, etc.).
int BuddyAllocator::order_of(size_t bytes) {
    if (bytes < PAGE_SIZE) return -1;
    size_t pages = bytes / PAGE_SIZE;
    if ((pages & (pages - 1)) != 0) return -1;

    int order = 0;
    while (pages > 1) {
        pages >>= 1;
        order++;
    }
    return order;
}

// Rounds requested size up to the nearest power-of-2 pages in bytes (minimum 1 Page = 64B).
size_t BuddyAllocator::round_to_buddy_size(size_t bytes) {
    if (bytes == 0) return PAGE_SIZE;
    size_t pages = (bytes + PAGE_SIZE - 1) / PAGE_SIZE;

    // Round pages up to next power of 2
    if ((pages & (pages - 1)) == 0) return pages * PAGE_SIZE;

    pages--;
    pages |= pages >> 1;
    pages |= pages >> 2;
    pages |= pages >> 4;
    pages |= pages >> 8;
    pages |= pages >> 16;
    pages |= pages >> 32;
    pages++;

    return pages * PAGE_SIZE;
}

// Initializes the buddy memory pool to a power-of-2 page size.
void BuddyAllocator::init(size_t size) {
    std::lock_guard<std::mutex> lock(alloc_mutex);
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

    total_size = round_to_buddy_size(size);
    total_pages = total_size / PAGE_SIZE;

    int max_order = order_of(total_size);
    free_lists.assign(max_order + 1, nullptr);
    next_id = 1;

    free_lists[max_order] = new BuddyBlock(0, total_size);

    std::cout << "[System] Page-Level Buddy Memory Initialized: "
              << total_size << " bytes (" << total_pages << " pages, Order " << max_order << ").\n";
}

// Allocates memory in units of 2^k pages by splitting larger buddy blocks down to requested order.
int BuddyAllocator::allocate(size_t size, Alloc_Algo) {
    std::lock_guard<std::mutex> lock(alloc_mutex);
    if (size == 0) return -1;

    size_t req_size = round_to_buddy_size(size);
    if (req_size > total_size) return -1;

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

    // Split down to the required page order
    while (current_order > req_order) {
        current_order--;
        size_t half = blk->size / 2;

        BuddyBlock* buddy = new BuddyBlock(blk->address + half, half);
        blk->size = half;
        blk->page_count = half / PAGE_SIZE;

        buddy->next = free_lists[current_order];
        free_lists[current_order] = buddy;
    }

    blk->id = next_id++;
    allocated[blk->id] = blk;
    return blk->id;
}

// Frees the allocated page block and recursively merges with its buddy page block.
void BuddyAllocator::deallocate(int id) {
    std::lock_guard<std::mutex> lock(alloc_mutex);
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

// Returns start address of allocated buddy page block.
size_t BuddyAllocator::get_address(int id) {
    std::lock_guard<std::mutex> lock(alloc_mutex);
    auto it = allocated.find(id);
    if (it == allocated.end()) return SIZE_MAX;
    return it->second->address;
}

// Displays free lists across all page order levels.
void BuddyAllocator::display() {
    std::lock_guard<std::mutex> lock(alloc_mutex);
    std::cout << "--- Page-Level Buddy Free Lists ---\n";
    for (size_t i = 0; i < free_lists.size(); i++) {
        size_t pages = (1ULL << i);
        size_t bytes = pages * PAGE_SIZE;
        std::cout << "Order " << i << " (" << pages << " Page" << (pages > 1 ? "s" : "")
                  << " = " << bytes << "B): ";
        BuddyBlock* curr = free_lists[i];
        while (curr) {
            std::cout << "[Addr:0x" << std::hex << std::uppercase << std::setfill('0') << std::setw(4)
                      << curr->address << std::dec
                      << ", Pages:" << curr->page_count
                      << ", Size:" << curr->size << "B] -> ";
            curr = curr->next;
        }
        std::cout << "nullptr\n";
    }
}

// Prints page-level memory metrics, allocated pages, and free memory breakdown.
void BuddyAllocator::get_statistics() {
    std::lock_guard<std::mutex> lock(alloc_mutex);
    size_t free_mem = 0;
    size_t free_blocks = 0;

    for (auto head : free_lists) {
        while (head) {
            free_mem += head->size;
            free_blocks++;
            head = head->next;
        }
    }

    size_t used_mem = total_size - free_mem;
    size_t used_pages = used_mem / PAGE_SIZE;
    size_t free_pages = free_mem / PAGE_SIZE;

    std::cout << "Total Memory Pool : " << total_size << " bytes (" << total_pages << " pages)\n";
    std::cout << "Allocated Blocks  : " << allocated.size() << " (" << used_pages << " pages, " << used_mem << " bytes)\n";
    std::cout << "Free Blocks       : " << free_blocks << " (" << free_pages << " pages, " << free_mem << " bytes)\n";
    std::cout << "Memory Utilization: " << std::fixed << std::setprecision(1)
              << (total_size > 0 ? (double)used_mem / total_size * 100.0 : 0.0) << "%\n";
}
