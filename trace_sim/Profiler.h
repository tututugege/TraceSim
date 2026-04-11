#pragma once
#include <cstdint>
#include <string>
#include <map>
#include <vector>
#include <iostream>
#include <iomanip>
#include "frontend/FrontendTypes.h"

// 后端阻塞归因细分
enum class BackendStallReason : uint8_t {
    NONE = 0,
    ROB_FULL,
    ALU_IQ_FULL,
    LDU_IQ_FULL,
    STA_IQ_FULL,
    STD_IQ_FULL,
    BRU_IQ_FULL,
    TRAP_SERIALIZE,
    MEMORY_WAIT,    // ROB 头部在等内存
    EXEC_WAIT,      // ROB 头部或 IQ 在等执行单元完成/数据依赖
};

class Profiler {
public:
    struct Stats {
        uint64_t total_branches = 0;
        uint64_t correct_branches = 0;
        uint64_t stlf_hits = 0;
        uint64_t mem_dep_stalls = 0;
    };

    enum class TopDownBucket {
        RETIRING,
        FRONTEND_BOUND,
        BACKEND_MEMORY_BOUND, // Level 2
        BACKEND_CORE_BOUND,   // Level 2
        BAD_SPECULATION,
        OTHER
    };

    Profiler();
    ~Profiler() = default;

    void inc_retired_insts(uint64_t count = 1) { retired_insts += count; }
    void inc_stlf_hits() { stats.stlf_hits++; }
    void inc_mem_dep_stalls() { stats.mem_dep_stalls++; }
    void inc_total_branches() { stats.total_branches++; }
    void inc_correct_branches() { stats.correct_branches++; }

    // 每周期归因上报
    // fe_active: 前端是否真的处于延迟中
    void mark_cycle(uint64_t cur_cycle, bool retired, 
                    FetchStallReason fe_reason, bool fe_active, 
                    BackendStallReason be_reason);

    void reset();
    void print_summary(uint32_t fetch_width, uint32_t rob_size);

    uint64_t get_total_cycles() const { return total_cycles; }
    uint64_t get_retired_insts() const { return retired_insts; }

private:
    uint64_t total_cycles = 0;
    uint64_t retired_insts = 0;
    
    Stats stats;
    std::map<TopDownBucket, uint64_t> topdown_counters;

    uint64_t total_cycles_baseline = 0;
    uint64_t retired_insts_baseline = 0;

    std::string bucket_to_string(TopDownBucket bucket);
};
