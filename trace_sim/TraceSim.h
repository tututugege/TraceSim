#pragma once
#include "../include/ref.h"
#include "../include/Trace.h"
#include <iostream>
#include <vector>
#include <deque>
#include <iomanip>
#include <memory>
#include "Cache.h"
#include "SimConfig.h"

#include "Cache.h"
#include "SimConfig.h"
#include "BranchPredictor.h"
#include "OpEntry.h"

class TraceSim {
public:
    Ref_cpu &ref_cpu;
    std::unique_ptr<BranchPredictor> bp;
    uint64_t total_cycles = 0;
    uint64_t instructions_retired = 0;
    uint64_t next_entry_id = 0;
    
    // Configuration
    uint32_t width;
    uint32_t rob_size;
    uint32_t alu_iq_size;
    uint32_t ldu_iq_size;
    uint32_t sta_iq_size;
    uint32_t std_iq_size;
    uint32_t bru_iq_size;
    uint64_t max_instructions;

    // Cache Models
    Cache icache;
    Cache dcache;

    // Registers readiness time
    std::vector<uint64_t> reg_ready_time;
    
    // Pipelines (Optimized: using indices into ROB circular buffer)
    std::vector<OpEntry> rob;
    uint32_t rob_head = 0;
    uint32_t rob_tail = 0;
    uint32_t rob_count = 0;

    std::vector<uint32_t> alu_iq;
    std::vector<uint32_t> ldu_iq;
    std::vector<uint32_t> sta_iq;
    std::vector<uint32_t> std_iq;
    std::vector<uint32_t> bru_iq;

    // Fast Store tracking for Load Disambiguation
    std::deque<uint32_t> store_indices; 
    
    // Fetch Stage
    std::deque<OpEntry> fetch_queue;
    std::deque<TraceInst> pre_fetch_buffer; // Temporarily store instructions from Ref_cpu
    uint64_t fetch_stall_until = 0;

    // Functional Units (limit issues per cycle)
    struct {
        uint32_t alu = TraceSimConfig::ALU_COUNT;
        uint32_t ldu = TraceSimConfig::LDU_COUNT;
        uint32_t sta = TraceSimConfig::STA_COUNT;
        uint32_t std = TraceSimConfig::STD_COUNT;
        uint32_t bru = TraceSimConfig::BRU_COUNT;
    } fu_width;

    SimMode mode;
    bool in_warmup = false;

    struct Stats {
        uint64_t total_branches = 0;
        uint64_t correct_branches = 0;
        uint64_t stlf_hits = 0;
        uint64_t mem_dep_stalls = 0;
    } stats;

    TraceSim(Ref_cpu &cpu, SimMode m, uint32_t w = TraceSimConfig::FETCH_WIDTH, 
             uint32_t rob_s = TraceSimConfig::ROB_SIZE, 
             uint32_t alu_iq_s = TraceSimConfig::ALU_IQ_SIZE,
             uint32_t ldu_iq_s = TraceSimConfig::LDU_IQ_SIZE,
             uint32_t sta_iq_s = TraceSimConfig::STA_IQ_SIZE,
             uint32_t std_iq_s = TraceSimConfig::STD_IQ_SIZE,
             uint32_t bru_iq_s = TraceSimConfig::BRU_IQ_SIZE,
             uint64_t max_insts = 0)
        : ref_cpu(cpu), mode(m), width(w), rob_size(rob_s), 
          alu_iq_size(alu_iq_s), ldu_iq_size(ldu_iq_s), sta_iq_size(sta_iq_s), std_iq_size(std_iq_s),
          bru_iq_size(bru_iq_s),
          max_instructions(max_insts),
          icache(TraceSimConfig::ICACHE_SIZE, TraceSimConfig::ICACHE_ASSOC, TraceSimConfig::ICACHE_LINE_SIZE), 
          dcache(TraceSimConfig::DCACHE_SIZE, TraceSimConfig::DCACHE_ASSOC, TraceSimConfig::DCACHE_LINE_SIZE) {
        reg_ready_time.resize(32, 0);
        rob.resize(rob_size);

        if (TraceSimConfig::BP_TYPE == TraceSimConfig::BP_Type::GSHARE) {
            bp = std::make_unique<GShareBranchPredictor>();
        } else {
            bp = std::make_unique<ProbabilisticBranchPredictor>(TraceSimConfig::BP_TARGET_ACCURACY);
        }

        if (mode == SimMode::RESTORE && TraceSimConfig::WARMUP_INSTRUCTIONS > 0) {
            in_warmup = true;
        }

        // Reserve space to avoid reallocations
        alu_iq.reserve(alu_iq_size);
        ldu_iq.reserve(ldu_iq_size);
        sta_iq.reserve(sta_iq_size);
        std_iq.reserve(std_iq_size);
        bru_iq.reserve(bru_iq_size);
    }

    uint64_t inst_retired_baseline = 0;
    uint64_t total_cycles_baseline = 0;
    struct {
        uint64_t icache_access = 0;
        uint64_t icache_hit = 0;
        uint64_t dcache_access = 0;
        uint64_t dcache_hit = 0;
    } cache_baselines;

    void reset_stats() {
        std::cout << "--- Warmup Finished, Capturing Statistics Baseline ---" << std::endl;
        inst_retired_baseline = instructions_retired;
        total_cycles_baseline = total_cycles;
        cache_baselines.icache_access = icache.access_count;
        cache_baselines.icache_hit = icache.hit_count;
        cache_baselines.dcache_access = dcache.access_count;
        cache_baselines.dcache_hit = dcache.hit_count;
        
        stats = {0, 0, 0, 0}; // These are fine to reset as they are additive only
    }

    FU_Type get_fu_type(uint32_t opcode) {
        if (opcode == 0b0000011 || opcode == 0b0100011 || opcode == 0b0101111) return FU_Type::LSU;
        if (opcode == 0b1100011 || opcode == 0b1101111 || opcode == 0b1100111) return FU_Type::BRU;
        return FU_Type::ALU;
    }

    uint32_t get_latency(uint32_t opcode) {
        if (opcode == 0b0000011) return TraceSimConfig::LDU_LATENCY; // Load
        return TraceSimConfig::ALU_LATENCY;
    }

    void print_stats() {
        uint64_t rel_inst = instructions_retired - inst_retired_baseline;
        uint64_t rel_cycles = total_cycles - total_cycles_baseline;
        uint64_t rel_icache_access = icache.access_count - cache_baselines.icache_access;
        uint64_t rel_icache_hit = icache.hit_count - cache_baselines.icache_hit;
        uint64_t rel_dcache_access = dcache.access_count - cache_baselines.dcache_access;
        uint64_t rel_dcache_hit = dcache.hit_count - cache_baselines.dcache_hit;

        std::cout << "\n--- Phase Simulation Statistics ---" << std::endl;
        std::cout << "Total Instructions: " << rel_inst << std::endl;
        std::cout << "Total Cycles: " << rel_cycles << std::endl;
        if (rel_cycles > 0) {
            std::cout << "Overall IPC: " << std::fixed << std::setprecision(2) 
                      << (double)rel_inst / rel_cycles << std::endl;
        }
        
        double inst_kilo = (double)rel_inst / 1000.0;

        if (stats.total_branches > 0) {
            double branch_misses = (double)(stats.total_branches - stats.correct_branches);
            std::cout << "Branch Accuracy: " << std::fixed << std::setprecision(2)
                      << (double)stats.correct_branches * 100.0 / stats.total_branches << "%" 
                      << " (MPKI: " << branch_misses / inst_kilo << ")" << std::endl;
        }
        std::cout << "STLF Hits: " << stats.stlf_hits << std::endl;
        std::cout << "Memory Dependency Stalls: " << stats.mem_dep_stalls << std::endl;
        
        uint64_t icache_misses = rel_icache_access - rel_icache_hit;
        double ic_hit_rate = rel_icache_access > 0 ? (double)rel_icache_hit * 100.0 / rel_icache_access : 0;
        std::cout << "I-Cache: " << rel_icache_access << " accesses, Hit Rate: " 
                  << std::fixed << std::setprecision(2) << ic_hit_rate << "%" 
                  << " (MPKI: " << (double)icache_misses / inst_kilo << ")" << std::endl;
        
        uint64_t dcache_misses = rel_dcache_access - rel_dcache_hit;
        double dc_hit_rate = rel_dcache_access > 0 ? (double)rel_dcache_hit * 100.0 / rel_dcache_access : 0;
        std::cout << "D-Cache: " << rel_dcache_access << " accesses, Hit Rate: " 
                  << std::fixed << std::setprecision(2) << dc_hit_rate << "%" 
                  << " (MPKI: " << (double)dcache_misses / inst_kilo << ")" << std::endl;
        std::cout << "-----------------------------\n" << std::endl;
    }

    void run();
};


