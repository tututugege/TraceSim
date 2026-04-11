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
#include "frontend/BranchPredictor.h"
#include "frontend/FrontendTypes.h"
#include "Profiler.h"
#include "OpEntry.h"

// Forward declarations for modular subsystems
class Frontend;
class LoadStoreUnit;
class Profiler;
class MemSubsystem;

class TraceSim {
public:
    friend class Frontend;
    friend class LoadStoreUnit;

    Ref_cpu &ref_cpu;
    std::unique_ptr<BranchPredictor> bp;
    std::unique_ptr<Profiler> profiler;
    std::unique_ptr<MemSubsystem> mem;

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
    BackendStallReason classify_commit_stall(bool retired_this_cycle) const;
    bool frontend_stall_active() const {
        return total_cycles < fetch_stall_until;
    }
    bool frontend_can_fetch() const {
        return !frontend_stall_active() && inst_buffer.size() < inst_buffer_size;
    }
    bool frontend_trace_exhausted() const {
        return ref_cpu.sim_end && pre_fetch_buffer.empty();
    }
    bool frontend_acquire_trace_inst(TraceInst &inst);
    void frontend_requeue_trace_inst(const TraceInst &inst);
    void push_inst_buffer(OpEntry op) { inst_buffer.push_back(std::move(op)); }
    void set_frontend_bound_hint(BoundReason reason) {
        frontend_bound_hint = reason;
    }
    uint32_t icache_line_size() const;
    bool is_register_ready(uint32_t reg) const {
        return reg_ready_time[reg] <= total_cycles;
    }
    void mark_register_ready(uint32_t reg, uint64_t ready_cycle) {
        if (reg != 0) {
            reg_ready_time[reg] = ready_cycle;
        }
    }
    void clear_load_memory_flags(OpEntry &op) const {
        op.waiting_on_memory = false;
        op.waiting_on_mem_dep = false;
    }
    void mark_waiting_on_memory(OpEntry &op) const {
        op.waiting_on_memory = true;
    }
    void mark_waiting_on_mem_dep(OpEntry &op) const {
        op.waiting_on_mem_dep = true;
    }
    void classify_load_if_needed(OpEntry &op) const {
        if (enable_dependent_load_profiling && !op.load_classified) {
            op.is_dependent_load = op.base_writer_valid && op.base_writer_is_load;
            op.load_classified = true;
        }
    }
    void mark_load_executed(OpEntry &op, uint64_t ready_cycle) {
        op.issue_cycle = total_cycles;
        op.execute_cycle = ready_cycle;
        mark_register_ready(op.inst.rd, ready_cycle);
        op.executed = true;
    }

    uint64_t inst_retired_baseline = 0;
    uint64_t total_cycles_baseline = 0;

    struct LoadProfileSnapshot {
        uint64_t total = 0;
        uint64_t dependent = 0;
        uint64_t stlf = 0;
        uint64_t l1_hit = 0;
        uint64_t l2_hit = 0;
        uint64_t dram = 0;
        uint64_t total_latency = 0;
    };

    LoadProfileSnapshot load_profile_baseline;
    LoadProfileSnapshot load_profile_stats;

    void record_load_event(OpEntry& op, bool dep, bool stlf, uint32_t lat, bool l1h, bool l2h, bool dram);

    FU_Type get_fu_type(uint32_t opcode) {
        if (opcode == 0b0000011 || opcode == 0b0100011 || opcode == 0b0101111) return FU_Type::LSU;
        if (opcode == 0b1100011 || opcode == 0b1101111 || opcode == 0b1100111) return FU_Type::BRU;
        return FU_Type::ALU;
    }

};

#include "frontend/Frontend.h"
#include "mem/LoadStoreUnit.h"
