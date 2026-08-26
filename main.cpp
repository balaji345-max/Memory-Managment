#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <iomanip>
#include <algorithm>

#include "src/MemoryAllocator.h"
#include "src/BuddyAllocator.h"
#include "src/SlabAllocator.h"
#include "src/Cache.h"
#include "src/VirtualMemory.h"
#include "src/MMap.h"
#include "src/TraceBenchmark.h"
#include "src/ThreadedBenchmark.h"
#include "src/Visualization.h"

// Splits a whitespace-delimited CLI command string into separate tokens.
std::vector<std::string> tokenize(const std::string& command) {
    std::stringstream ss(command);
    std::string temp;
    std::vector<std::string> tokens;
    while (ss >> temp) tokens.push_back(temp);
    return tokens;
}

// Entry point for interactive CLI simulator managing allocators, cache hierarchy, MMU, mmap, and benchmarks.
int main() {
    MemoryAllocator linear_alloc;
    BuddyAllocator buddy_alloc;
    SlabAllocator slab_alloc;
    Allocator* current_allocator = &linear_alloc;
    Alloc_Algo current_strategy = Firstfit;

    CacheLevel l1(1, 64, 8, 1, LRU);    
    CacheLevel l2(2, 256, 16, 2, LRU);  
    CacheLevel l3(3, 512, 32, 4, LRU);  
    MemoryHierarchy cache_system(&l1, &l2, &l3);

    VirtualMemory mmu(&cache_system, VM_LRU);
    TLB tlb(16, 4); 

    MMapManager mmap_mgr(VIRTUAL_MEM_SIZE / 2, VIRTUAL_MEM_SIZE);
    mmu.set_mmap_manager(&mmap_mgr);

    TraceBenchmark bench;
    ThreadedBenchmark threaded_bench;
    bool auto_viz = false;

    std::string line;
    bool is_initialized = false;
    size_t system_memory_size = 0;

    std::cout << "====================================================\n";
    std::cout << "   Memory Management Simulator CLI Started\n";
    std::cout << "   Commands:\n";
    std::cout << "   - init memory <size>\n";
    std::cout << "   - set cache_policy <LRU|FIFO|LFU>\n";
    std::cout << "   - set page_policy <LRU|FIFO|CLOCK>\n";
    std::cout << "   - set allocator <buddy|slab|first_fit|best_fit|worst_fit>\n";
    std::cout << "   - set pagetable <flat|multilevel>\n";
    std::cout << "   - set viz <on|off>\n";
    std::cout << "   - malloc <size> | free <id> | stats\n";
    std::cout << "   - read <v_addr> | write <v_addr>\n";
    std::cout << "   - mmap <length> [r|rw|rwx] [file <name>]\n";
    std::cout << "   - munmap <addr> | mprotect <addr> <r|rw|rwx|none>\n";
    std::cout << "   - mmap list | create_file <name> <size>\n";
    std::cout << "   - bench generate <pattern> <count> [addr_range]\n";
    std::cout << "   - bench load <file> | bench run | bench report\n";
    std::cout << "   - bench threaded <threads> <ops>  (full pipeline stress)\n";
    std::cout << "   - bench alloc_stress <threads> <ops>\n";
    std::cout << "   - bench cache_stress <threads> <ops>\n";
    std::cout << "   - viz memory|frag|cache|mmap|all\n";
    std::cout << "   - dump memory | exit\n";
    std::cout << "====================================================\n";

    while (true) {
        std::cout << "> ";
        if (!std::getline(std::cin, line)) break;
        if (line.empty()) continue;

        auto tokens = tokenize(line);
        if (tokens.empty()) continue;
        std::string cmd = tokens[0];

        // ==================== exit ====================
        if (cmd == "exit") {
            break;
        }

        // ==================== init ====================
        if (cmd == "init" && tokens.size() >= 3 && tokens[1] == "memory") {
            try {
                system_memory_size = std::stoul(tokens[2]);
                linear_alloc.init(system_memory_size);
                buddy_alloc.init(system_memory_size);
                slab_alloc.init(system_memory_size);
                is_initialized = true;
                std::cout << "Physical memory initialized to " << system_memory_size << " bytes.\n";
            } catch (...) {
                std::cout << "Error: Invalid memory size.\n";
            }
            continue;
        }

        if (!is_initialized) {
            std::cout << "Error: Memory not initialized. Run 'init memory <size>' first.\n";
            continue;
        }

        // ==================== set commands ====================
        if (cmd == "set" && tokens.size() >= 3) {
            if (tokens[1] == "cache_policy") {
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
            else if (tokens[1] == "page_policy") {
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
            else if (tokens[1] == "allocator") {
                std::string strat = tokens[2];
                if (strat == "buddy") {
                    current_allocator = &buddy_alloc;
                    std::cout << "Allocator set to Buddy System.\n";
                } else if (strat == "slab") {
                    current_allocator = &slab_alloc;
                    std::cout << "Allocator set to Slab Allocator.\n";
                } else {
                    current_allocator = &linear_alloc;
                    if (strat == "best_fit") current_strategy = Bestfit;
                    else if (strat == "worst_fit") current_strategy = Worstfit;
                    else current_strategy = Firstfit;
                    std::cout << "Allocator set to Linear (" << strat << ").\n";
                }
            }
            else if (tokens[1] == "pagetable") {
                std::string mode = tokens[2];
                if (mode == "multilevel" || mode == "multi" || mode == "4level") {
                    mmu.set_multi_level_paging(true);
                } else {
                    mmu.set_multi_level_paging(false);
                }
            }
            else if (tokens[1] == "viz") {
                auto_viz = (tokens[2] == "on");
                std::cout << "Auto-visualization " << (auto_viz ? "enabled" : "disabled") << ".\n";
            }
            continue;
        }

        // ==================== malloc ====================
        if (cmd == "malloc" && tokens.size() >= 2) {
            int id = current_allocator->allocate(std::stoul(tokens[1]), current_strategy);
            if (id != -1) {
                std::cout << "Allocated block id=" << id << " at address=0x" 
                          << std::hex << std::setfill('0') << std::setw(4) << current_allocator->get_address(id) 
                          << std::dec << "\n";
                if (auto_viz) MemoryVisualizer::render_all(current_allocator, &cache_system, &mmap_mgr);
            } else {
                std::cout << "Allocation failed.\n";
            }
            continue;
        }

        // ==================== free ====================
        if (cmd == "free" && tokens.size() >= 2) {
            current_allocator->deallocate(std::stoi(tokens[1]));
            std::cout << "Block " << tokens[1] << " freed.\n";
            if (auto_viz) MemoryVisualizer::render_all(current_allocator, &cache_system, &mmap_mgr);
            continue;
        }

        // ==================== read / write ====================
        if (cmd == "read" || cmd == "write") {
            if (tokens.size() < 2) continue;
            u64 v_addr = std::stoull(tokens[1]);

            // Check mmap protection
            if (mmap_mgr.contains(v_addr)) {
                if (!mmap_mgr.check_access(v_addr, cmd == "write")) {
                    std::cout << "[MMU] Protection Fault: " << cmd << " not allowed at 0x"
                              << std::hex << v_addr << std::dec << "\n";
                    continue;
                }
            }

            std::string translation_report;
            ll p_addr = mmu.translate(v_addr, (cmd == "write"), tlb, translation_report);
            
            std::cout << "[MMU] " << translation_report << "\n";
            if (p_addr != -1) {
                std::cout << "[Cache] " << cache_system.request(static_cast<size_t>(p_addr), (cmd == "write")) << "\n";
            }
            continue;
        }

        // ==================== mmap ====================
        if (cmd == "mmap") {
            if (tokens.size() >= 2 && tokens[1] == "list") {
                mmap_mgr.display_mappings();
                continue;
            }
            if (tokens.size() < 2) {
                std::cout << "Usage: mmap <length> [r|rw|rwx] [file <name>]\n";
                continue;
            }

            u64 length = std::stoull(tokens[1]);
            int prot = MM_PROT_READ | MM_PROT_WRITE;  // default: rw
            int flags = MM_MAP_PRIVATE | MM_MAP_ANONYMOUS;
            std::string filename;

            // Parse optional protection
            if (tokens.size() >= 3) {
                std::string p = tokens[2];
                if (p == "r") prot = MM_PROT_READ;
                else if (p == "rw") prot = MM_PROT_READ | MM_PROT_WRITE;
                else if (p == "rwx") prot = MM_PROT_READ | MM_PROT_WRITE | MM_PROT_EXEC;
                else if (p == "none") prot = MM_PROT_NONE;
            }

            // Parse optional file backing
            if (tokens.size() >= 5 && tokens[3] == "file") {
                filename = tokens[4];
                flags = MM_MAP_PRIVATE;  // file-backed, remove ANONYMOUS
            }

            u64 addr = mmap_mgr.do_mmap(0, length, prot, flags, filename);
            if (addr != static_cast<u64>(-1)) {
                std::cout << "[mmap] Mapped " << length << " bytes at 0x"
                          << std::hex << addr << std::dec << "\n";
            }
            continue;
        }

        // ==================== munmap ====================
        if (cmd == "munmap" && tokens.size() >= 2) {
            u64 addr = std::stoull(tokens[1]);
            if (mmap_mgr.do_munmap(addr) == 0) {
                std::cout << "[munmap] Unmapped region at 0x" << std::hex << addr << std::dec << "\n";
            }
            continue;
        }

        // ==================== mprotect ====================
        if (cmd == "mprotect" && tokens.size() >= 3) {
            u64 addr = std::stoull(tokens[1]);
            int new_prot = MM_PROT_NONE;
            std::string p = tokens[2];
            if (p == "r") new_prot = MM_PROT_READ;
            else if (p == "rw") new_prot = MM_PROT_READ | MM_PROT_WRITE;
            else if (p == "rwx") new_prot = MM_PROT_READ | MM_PROT_WRITE | MM_PROT_EXEC;

            if (mmap_mgr.do_mprotect(addr, new_prot) == 0) {
                std::cout << "[mprotect] Protection updated for 0x" << std::hex << addr << std::dec << "\n";
            }
            continue;
        }

        // ==================== create_file ====================
        if (cmd == "create_file" && tokens.size() >= 3) {
            mmap_mgr.create_file(tokens[1], std::stoul(tokens[2]));
            continue;
        }

        // ==================== bench ====================
        if (cmd == "bench" && tokens.size() >= 2) {
            if (tokens[1] == "generate" && tokens.size() >= 4) {
                std::string pattern = tokens[2];
                size_t count = std::stoul(tokens[3]);
                u64 addr_range = (tokens.size() >= 5) ? std::stoull(tokens[4]) : VIRTUAL_MEM_SIZE;
                bench.generate_trace(pattern, count, addr_range);
            }
            else if (tokens[1] == "load" && tokens.size() >= 3) {
                bench.load_trace(tokens[2]);
            }
            else if (tokens[1] == "run") {
                if (bench.trace_size() == 0) {
                    std::cout << "[Bench] No trace loaded. Use 'bench load' or 'bench generate' first.\n";
                } else {
                    std::cout << "[Bench] Running " << bench.trace_size() << " trace entries...\n";
                    bench.run(current_allocator, cache_system, mmu, tlb, current_strategy);
                    bench.report();
                }
            }
            else if (tokens[1] == "report") {
                bench.report();
            }
            else if (tokens[1] == "threaded" && tokens.size() >= 4) {
                int nthreads = std::stoi(tokens[2]);
                int ops = std::stoi(tokens[3]);
                std::cout << "[ThreadBench] Running " << nthreads << " threads × " << ops << " ops (full pipeline)...\n";
                threaded_bench.run(nthreads, ops, current_allocator, cache_system, mmu, tlb, current_strategy);
                threaded_bench.report();
            }
            else if (tokens[1] == "alloc_stress" && tokens.size() >= 4) {
                int nthreads = std::stoi(tokens[2]);
                int ops = std::stoi(tokens[3]);
                std::cout << "[ThreadBench] Running " << nthreads << " threads × " << ops << " ops (allocator stress)...\n";
                threaded_bench.run_alloc_stress(nthreads, ops, current_allocator, current_strategy);
                threaded_bench.report();
            }
            else if (tokens[1] == "cache_stress" && tokens.size() >= 4) {
                int nthreads = std::stoi(tokens[2]);
                int ops = std::stoi(tokens[3]);
                std::cout << "[ThreadBench] Running " << nthreads << " threads × " << ops << " ops (cache stress)...\n";
                threaded_bench.run_cache_stress(nthreads, ops, cache_system);
                threaded_bench.report();
            }
            else {
                std::cout << "Usage: bench <generate|load|run|report|threaded|alloc_stress|cache_stress> ...\n";
            }
            continue;
        }

        // ==================== viz ====================
        if (cmd == "viz" && tokens.size() >= 2) {
            if (tokens[1] == "memory") {
                auto* linear = dynamic_cast<MemoryAllocator*>(current_allocator);
                auto* buddy = dynamic_cast<BuddyAllocator*>(current_allocator);
                auto* slab = dynamic_cast<SlabAllocator*>(current_allocator);
                if (linear) MemoryVisualizer::render_linear(linear);
                else if (buddy) MemoryVisualizer::render_buddy(buddy);
                else if (slab) MemoryVisualizer::render_slab(slab);
            }
            else if (tokens[1] == "frag") {
                auto* linear = dynamic_cast<MemoryAllocator*>(current_allocator);
                if (linear) MemoryVisualizer::render_fragmentation(linear);
                else std::cout << "Fragmentation gauge only available for linear allocator.\n";
            }
            else if (tokens[1] == "cache") {
                MemoryVisualizer::render_cache_heatmap(&cache_system);
            }
            else if (tokens[1] == "mmap") {
                MemoryVisualizer::render_mmap(&mmap_mgr);
            }
            else if (tokens[1] == "pages") {
                MemoryVisualizer::render_page_map(VIRTUAL_MEM_SIZE / PAGE_SIZE, PHYSICAL_MEM_SIZE / PAGE_SIZE);
            }
            else if (tokens[1] == "all") {
                MemoryVisualizer::render_all(current_allocator, &cache_system, &mmap_mgr);
            }
            else {
                std::cout << "Usage: viz <memory|frag|cache|mmap|pages|all>\n";
            }
            continue;
        }

        // ==================== stats ====================
        if (cmd == "stats") {
            current_allocator->get_statistics();
            cache_system.display_all_stats();
            mmu.get_statistics();
            continue;
        }

        // ==================== dump ====================
        if (cmd == "dump" && tokens.size() >= 2 && tokens[1] == "memory") {
            current_allocator->display();
            continue;
        }

        std::cout << "Unknown command: " << cmd << "\n";
    }

    return 0;
}