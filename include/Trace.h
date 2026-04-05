#pragma once
#include <cstdint>

struct TraceInst {
    uint32_t pc;
    uint32_t opcode;
    uint32_t rd;
    uint32_t rs1;
    uint32_t rs2;
    bool is_branch;
    bool branch_taken;
    uint32_t target;
    bool is_load;
    bool is_store;
    bool is_trap;
    bool is_wfi;   
    bool is_ebreak; // Added for termination
    uint32_t mem_addr;
};
