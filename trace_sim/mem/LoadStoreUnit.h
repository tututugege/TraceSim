#pragma once
#include <cstdint>
#include "../OpEntry.h"

class TraceSim; // Forward declaration

class LoadStoreUnit {
public:
    enum class LoadIssueResult : uint8_t {
        Issued = 0,
        STLFHit,
        StalledByDep,
        StalledByMSHR,
        NotReady
    };
    
    enum class StoreDepResult : uint8_t {
        NoDep = 0,
        STLFReady,
        Stalled
    };

    explicit LoadStoreUnit(TraceSim& simulator) : sim(simulator) {}

    void issue_stage();
    LoadIssueResult try_issue_load(uint32_t rob_idx);

private:
    TraceSim& sim;
    
    StoreDepResult check_store_dependency(OpEntry &op);
    LoadIssueResult execute_load_memory_access(OpEntry &op);
    void finalize_load_profile(OpEntry &op, uint32_t latency, bool l1_hit, bool l2_hit, bool dram_miss, bool is_stlf_hit);
};
