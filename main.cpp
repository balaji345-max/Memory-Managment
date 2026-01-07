#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <iomanip>
#include <algorithm>

#include "src/MemoryAllocator.h"
#include "src/BuddyAllocator.h"
#include "src/Cache.h"
#include "src/VirtualMemory.h"

std::vector<std::string> tokenize(const std::string& command) {
    std::stringstream ss(command);
    std::string temp;
    std::vector<std::string> tokens;
    while (ss >> temp) tokens.push_back(temp);
    return tokens;
}

int main() {
    MemoryAllocator linear_alloc;
    BuddyAllocator buddy_alloc;
    Allocator* current_allocator = &linear_alloc;
    Alloc_Algo current_strategy = Firstfit;

    CacheLevel l1(1, 64, 8, 1, LRU);    
    CacheLevel l2(2, 256, 16, 2, LRU);  
    CacheLevel l3(3, 512, 32, 4, LRU);  
    MemoryHierarchy cache_system(&l1, &l2, &l3);

   VirtualMemory mmu(&cache_system, VM_LRU);
    TLB tlb(16, 4); 

    std::string line;
    bool is_initialized = false;
    size_t system_memory_size = 0;

    std::cout << "====================================================\n";
    std::cout << "   Memory Management Simulator CLI Started\n";
    std::cout << "   Commands:\n";
    std::cout << "   - init memory <size>\n";
    std::cout << "   - set cache_policy <LRU|FIFO|LFU>\n";
    std::cout << "   - set page_policy <LRU|FIFO|CLOCK>\n";
    std::cout << "   - set allocator <buddy|first_fit|best_fit|worst_fit>\n";
    std::cout << "   - malloc <size> | free <id> | stats\n";
    std::cout << "   - read <v_addr> | write <v_addr>\n";
    std::cout << "   - dump memory | exit\n";
    std::cout << "====================================================\n";

    while (true) {
        std::cout << "> ";
        if (!std::getline(std::cin, line)) break;
        if (line.empty()) continue;

        auto tokens = tokenize(line);
        if (tokens.empty()) continue;
        std::string cmd = tokens[0];

        if (cmd == "exit") break;

        else if (cmd == "init" && tokens.size() >= 3 && tokens[1] == "memory") {
            try {
                system_memory_size = std::stoul(tokens[2]);
                linear_alloc.init(system_memory_size);
                buddy_alloc.init(system_memory_size);
                is_initialized = true;
                std::cout << "Physical memory initialized to " << system_memory_size << " bytes.\n";
            } catch (...) {
                std::cout << "Error: Invalid memory size.\n";
            }
        }

        else if (!is_initialized) {
            std::cout << "Error: Memory not initialized. Run 'init memory <size>' first.\n";
        }

        else if (cmd == "set" && tokens.size() >= 3 && tokens[1] == "cache_policy") {
            std::string policy_str = tokens[2];
            std::transform(policy_str.begin(), policy_str.end(), policy_str.begin(), ::toupper);

            ReplacementPolicy new_policy;
            bool valid = true;

            if (policy_str == "LRU") new_policy = LRU;
            else if (policy_str == "FIFO") new_policy = FIFO;
            else if (policy_str == "LFU") new_policy = LFU;
            else {
                std::cout << "Error: Unknown cache policy '" << policy_str << "'. Use LRU, FIFO, or LFU.\n";
                valid = false;
            }

            if (valid) {
                l1.set_policy(new_policy);
                l2.set_policy(new_policy);
                l3.set_policy(new_policy);
                std::cout << "Cache replacement policy set to " << policy_str << " for all levels.\n";
            }
        }

        else if (cmd == "set" && tokens.size() >= 3 && tokens[1] == "page_policy") {
            std::string policy_str = tokens[2];
            std::transform(policy_str.begin(), policy_str.end(), policy_str.begin(), ::toupper);

            if (policy_str == "LRU") {
                mmu.set_replacement_policy(VM_LRU);
                std::cout << "Page replacement policy set to LRU.\n";
            } else if (policy_str == "FIFO") {
                mmu.set_replacement_policy(VM_FIFO);
                std::cout << "Page replacement policy set to FIFO.\n";
            } else if (policy_str == "CLOCK") {
                mmu.set_replacement_policy(VM_CLOCK);
                std::cout << "Page replacement policy set to CLOCK.\n";
            } else {
                std::cout << "Error: Unknown page policy '" << policy_str << "'. Use LRU, FIFO, or CLOCK.\n";
            }
        }

        else if (cmd == "set" && tokens.size() >= 3 && tokens[1] == "allocator") {
            std::string strat = tokens[2];
            if (strat == "buddy") {
                current_allocator = &buddy_alloc;
                std::cout << "Allocator set to Buddy System.\n";
            } else {
                current_allocator = &linear_alloc;
                if (strat == "best_fit") current_strategy = Bestfit;
                else if (strat == "worst_fit") current_strategy = Worstfit;
                else current_strategy = Firstfit;
                std::cout << "Allocator set to Linear (" << strat << ").\n";
            }
        }

        else if (cmd == "malloc" && tokens.size() >= 2) {
            int id = current_allocator->allocate(std::stoul(tokens[1]), current_strategy);
            if (id != -1) {
                std::cout << "Allocated block id=" << id << " at address=0x" 
                          << std::hex << std::setfill('0') << std::setw(4) << current_allocator->get_address(id) 
                          << std::dec << "\n";
            } else {
                std::cout << "Allocation failed.\n";
            }
        }

        else if (cmd == "free" && tokens.size() >= 2) {
            current_allocator->deallocate(std::stoi(tokens[1]));
            std::cout << "Block " << tokens[1] << " freed.\n";
        }

        else if (cmd == "read" || cmd == "write") {
            if (tokens.size() < 2) continue;
            u64 v_addr = std::stoull(tokens[1]);
            std::string translation_report;
            ll p_addr = mmu.translate(v_addr, (cmd == "write"), tlb, translation_report);
            
            std::cout << "[MMU] " << translation_report << "\n";
            if (p_addr != -1) {
                std::cout << "[Cache] " << cache_system.request((size_t)p_addr, (cmd == "write")) << "\n";
            }
        }

        else if (cmd == "stats") {
            current_allocator->get_statistics();
            cache_system.display_all_stats();
            mmu.get_statistics();
        }

        else if (cmd == "dump" && tokens.size() >= 2 && tokens[1] == "memory") {
            current_allocator->display();
        }

        else {
            std::cout << "Unknown command: " << cmd << "\n";
        }
    }

    return 0;
}