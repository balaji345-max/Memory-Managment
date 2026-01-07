#include "MemoryAllocator.h"
#include <iostream>
#include <limits>
#include <iomanip>
MemoryAllocator::MemoryAllocator() : total_size(0), head(nullptr), next_Id(1) {}
MemoryAllocator::~MemoryAllocator() {
    Mem_Block* curr = head;
    while (curr) {
        Mem_Block* temp = curr;
        curr = curr->next;
        delete temp;
    }
    head = nullptr;
}
size_t MemoryAllocator::get_address(int id) {
    if (id_map.find(id) != id_map.end()) {
        return id_map[id]->start_address;
    }
    return 0; 
}
void MemoryAllocator::init(size_t mem_size) {
    if (head) {
        Mem_Block* curr = head;
        while (curr) {
            Mem_Block* temp = curr;
            curr = curr->next;
            delete temp;
        }
        head = nullptr;
    }
    
    id_map.clear();
    
    if (mem_size == 0) return;

    total_size = mem_size;
    next_Id = 1;
    // Create the initial giant free block
    head = new Mem_Block(0, mem_size, 0, true, 0);
    std::cout << "[System] Linear Memory Initialized: " << mem_size << " bytes.\n";
}

int MemoryAllocator::allocate(size_t mem_size, Alloc_Algo algo) {
    if (mem_size == 0) return -1;
    total_alloc_attempts++; // Track attempt

   // size_t aligned_size = (mem_size + (ALIGNMENT - 1)) & ~(ALIGNMENT - 1);
   size_t aligned_size=mem_size;
    Mem_Block* best = nullptr;
    Mem_Block* curr = head;

    while (curr) {
        if (curr->is_free && curr->mem_size >= aligned_size) {
            if (algo == Firstfit) { best = curr; break; }
            if (algo == Bestfit) { if (!best || curr->mem_size < best->mem_size) best = curr; }
            else if (algo == Worstfit) { if (!best || curr->mem_size > best->mem_size) best = curr; }
        }
        curr = curr->next;
    }

    if (!best) return -1;

    if (best->mem_size > aligned_size) {
        Mem_Block* new_free = new Mem_Block(best->start_address + aligned_size, 
                                            best->mem_size - aligned_size, 0, true, 0);
        new_free->next = best->next;
        new_free->prev = best;
        if (best->next) best->next->prev = new_free;
        best->next = new_free;
        best->mem_size = aligned_size;
    }

    best->is_free = false;
    best->Id = next_Id++;
    best->req_size = mem_size; 
    id_map[best->Id] = best;
    
    successful_allocations++; // Track success
    return best->Id;
}void MemoryAllocator::deallocate(int Id) {
    if (id_map.find(Id) == id_map.end()) return;

    Mem_Block* curr = id_map[Id];
    curr->is_free = true;
    curr->Id = 0;
    curr->req_size = 0;
    id_map.erase(Id);

    // Coalesce with next block if it is free
    if (curr->next && curr->next->is_free) {
        Mem_Block* next_block = curr->next;
        curr->mem_size += next_block->mem_size;
        curr->next = next_block->next;
        if (next_block->next) next_block->next->prev = curr;
        delete next_block;
    }

    // Coalesce with previous block if it is free
    if (curr->prev && curr->prev->is_free) {
        Mem_Block* prev_block = curr->prev;
        prev_block->mem_size += curr->mem_size;
        prev_block->next = curr->next;
        if (curr->next) curr->next->prev = prev_block;
        delete curr;
    }
}

void MemoryAllocator::display() {
    Mem_Block* curr = head;
    while (curr) {
        std::cout << "[" << std::hex << std::uppercase << "0x" << std::setfill('0') << std::setw(4) << curr->start_address
                  << " - 0x" << std::setw(4) << (curr->start_address + curr->mem_size - 1) << "] "
                  << std::dec << (curr->is_free ? "FREE" : "USED (id=" + std::to_string(curr->Id) + ")") 
                  << "\n";
        curr = curr->next;
    }
}

void MemoryAllocator::get_statistics() {
    size_t total_free = 0, used = 0, internal_frag = 0;
    size_t largest_free_block = 0; 

    Mem_Block* curr = head;
    while (curr) {
        if (curr->is_free) {
            total_free += curr->mem_size;
            if (curr->mem_size > largest_free_block) largest_free_block = curr->mem_size;
        } else {
            used += curr->mem_size;
            internal_frag += (curr->mem_size - curr->req_size);
        }
        curr = curr->next;
    }
    
    double ext_frag_perc = (total_free > 0) 
        ? (double)(total_free - largest_free_block) / total_free * 100.0 
        : 0.0;
    double utilization = total_size > 0 ? (double)used / total_size * 100.0 : 0;
    double success_rate = total_alloc_attempts > 0 ? (double)successful_allocations / total_alloc_attempts * 100.0 : 0;

    std::cout << "Total memory: " << total_size << "\n";
    std::cout << "Used memory: " << used << "\n";
    std::cout << "Internal fragmentation: " << internal_frag << "\n";
    std::cout << "External fragmentation: " << std::fixed << std::setprecision(0) << ext_frag_perc << "%\n";
    std::cout << "Allocation success rate: " << success_rate << "%\n";
    std::cout << "Memory utilization: " << utilization << "%\n";
}