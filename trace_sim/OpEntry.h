#pragma once
#include <cstdint>
#include "../include/Trace.h"

enum class FU_Type { ALU, LSU, BRU };

struct OpEntry {
    TraceInst inst;
    uint64_t entry_id;
    uint64_t fetch_cycle;
    uint64_t dispatch_cycle;
    uint64_t issue_cycle;
    uint64_t execute_cycle;
    uint64_t commit_cycle;
    
    FU_Type fu_type;
    bool executed = false;
    bool committed = false;

    // Memory Logic
    bool agu_done = false;
    uint64_t agu_cycle;

    // Specialized Memory Units (Cracking for Stores)
    bool sta_done = false;
    bool std_done = false;
    uint64_t sta_cycle;
    uint64_t std_cycle;
};
