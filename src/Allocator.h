#pragma once
#include <iostream>
#include <vector>
#include <unordered_map>
#include <algorithm>
 enum Alloc_Algo{Firstfit,Bestfit,Worstfit};
 class Allocator{
    public:
    virtual void init(size_t mem_size)=0;// delete all old nodes and creates single Giant free block.
    virtual int allocate(size_t mem_size,Alloc_Algo algo)=0;// returns Block Id.
    virtual void deallocate(int block_id) = 0;
    virtual size_t get_address(int block_id) = 0; 
    virtual void display()=0; // prints the state of Linked list.
    virtual void get_statistics()=0; // print metrics .
    virtual ~Allocator() {};
 };