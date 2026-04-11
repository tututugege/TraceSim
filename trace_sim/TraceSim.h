#pragma once
#include "../include/ref.h"
#include "../include/Trace.h"
#include <iostream>
#include <vector>
#include <deque>
#include <iomanip>
#include <memory>
#include <unordered_map>
#include <algorithm>
#include <array>

#include "SimConfig.h"
#include "mem/Cache.h"
#include "frontend/BranchPredictor.h"
#include "frontend/FrontendTypes.h"
#include "Profiler.h"
#include "OpEntry.h"

// Forward declarations for modular subsystems
class Frontend;
class LoadStoreUnit;
class Profiler;

class TraceSim {
public:
    friend class Frontend;
    friend class LoadStoreUnit;

    Ref_cpu &ref_cpu;
    std::unique_ptr<BranchPredictor> bp;
    std::unique_ptr<Profiler> profiler;

    uint64_t total_cycles = 0;
    uint64_t instructions_retired = 0;
    uint64_t next_entry_id = 0;
    
    // Configuration
    uint32_t fetch_bandwidth;
    uint32_t dispatch_width;
    uint32_t commit_width;
    uint32_t rob_size;
    uint32_t alu_iq_size;
    uint32_t ldu_iq_size;
    uint32_t sta_iq_size;
    uint32_t std_iq_size;
    uint32_t bru_iq_size;
    uint32_t inst_buffer_size;
    uint64_t max_instructions;

    // Registers readiness time
    std::vector<uint64_t> reg_ready_time;
    
    // Pipelines
    std::vector<OpEntry> rob;
    uint32_t rob_head = 0;
    uint32_t rob_tail = 0;
    uint32_t rob_count = 0;

    std::vector<uint32_t> alu_iq;
    std::vector<uint32_t> ldu_iq;
    std::vector<uint32_t> sta_iq;
    std::vector<uint32_t> std_iq;
    std::vector<uint32_t> bru_iq;

    std::deque<uint32_t> store_indices; 
    
    // Buffers and Queues
    std::deque<OpEntry> inst_buffer;
    std::deque<TraceInst> pre_fetch_buffer;
    std::deque<OpEntry> fetch_queue;
    
    // Stall logic
    uint64_t fetch_stall_until = 0;
    FetchStallReason fetch_stall_reason = FetchStallReason::NONE;
    BoundReason frontend_bound_hint = BoundReason::NONE;

    // Cache Models
    Cache icache;
    Cache dcache;
    Cache llc;

    struct {
        uint32_t alu = TraceSimConfig::ALU_COUNT;
        uint32_t ldu = TraceSimConfig::LDU_COUNT;
        uint32_t sta = TraceSimConfig::STA_COUNT;
        uint32_t std = TraceSimConfig::STD_COUNT;
        uint32_t bru = TraceSimConfig::BRU_COUNT;
    } fu_width;

    SimMode mode;
    bool in_warmup = false;
    bool enable_dependent_load_profiling;

    struct RegWriterInfo {
        bool valid = false;
        uint64_t entry_id = 0;
        bool is_load = false;
        uint32_t pc = 0;
    };
    std::vector<RegWriterInfo> reg_last_writer;
    std::vector<RegWriterInfo> reg_last_committed_writer;

    // Subsystems
    std::unique_ptr<Frontend> frontend;
    std::unique_ptr<LoadStoreUnit> lsu;

    TraceSim(Ref_cpu &cpu, SimMode m, 
             uint32_t fetch_w = TraceSimConfig::FETCH_WIDTH, 
             uint32_t dispatch_w = TraceSimConfig::DISPATCH_WIDTH,
             uint32_t commit_w = TraceSimConfig::COMMIT_WIDTH,
             uint32_t rob_s = TraceSimConfig::ROB_SIZE, 
             uint32_t alu_iq_size = TraceSimConfig::ALU_IQ_SIZE,
             uint32_t ldu_iq_size = TraceSimConfig::LDU_IQ_SIZE,
             uint32_t sta_iq_size = TraceSimConfig::STA_IQ_SIZE,
             uint32_t std_iq_size = TraceSimConfig::STD_IQ_SIZE,
             uint32_t bru_iq_size = TraceSimConfig::BRU_IQ_SIZE,
             uint64_t max_insts = 0,
             bool enable_dep_load_profile = TraceSimConfig::ENABLE_DEPENDENT_LOAD_PROFILING);

    ~TraceSim();

    void set_fetch_stall(uint64_t until, FetchStallReason reason) {
        if (until > fetch_stall_until) {
            fetch_stall_until = until;
            fetch_stall_reason = reason;
        }
    }

    void run();
    void reset_stats();
    void print_stats();
    
    // Core Pipeline Stages
    void commit_stage();
    void issue_stage();
    BackendStallReason dispatch_stage();
    void decode_stage();
    void advance_cycle();

    void process_cache_returns() {
        icache.process_returns(total_cycles);
        dcache.process_returns(total_cycles);
        llc.process_returns(total_cycles);
    }

    uint64_t inst_retired_baseline = 0;
    uint64_t total_cycles_baseline = 0;
    struct {
        uint64_t icache_access = 0, icache_hit = 0;
        uint64_t dcache_access = 0, dcache_hit = 0;
        uint64_t llc_access = 0, llc_hit = 0;
    } cache_baselines;

    void record_load_event(OpEntry& op, bool dep, bool stlf, uint32_t lat, bool l1h, bool l2h, bool dram);

    FU_Type get_fu_type(uint32_t opcode) {
        if (opcode == 0b0000011 || opcode == 0b0100011 || opcode == 0b0101111) return FU_Type::LSU;
        if (opcode == 0b1100011 || opcode == 0b1101111 || opcode == 0b1100111) return FU_Type::BRU;
        return FU_Type::ALU;
    }

    void enqueue_prefetches(Cache &l1, uint32_t pc, uint32_t addr, bool hit, bool is_data);
    void service_prefetch_queues();
};

#include "frontend/Frontend.h"
#include "mem/LoadStoreUnit.h"
