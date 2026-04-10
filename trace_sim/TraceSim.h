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
    Cache llc;

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
    bool enable_dependent_load_profiling = TraceSimConfig::ENABLE_DEPENDENT_LOAD_PROFILING;

    struct Stats {
        uint64_t total_branches = 0;
        uint64_t correct_branches = 0;
        uint64_t stlf_hits = 0;
        uint64_t mem_dep_stalls = 0;
    } stats;

    struct RegWriterInfo {
        bool valid = false;
        uint64_t entry_id = 0;
        bool is_load = false;
        uint32_t pc = 0;
    };

    enum class LoadBucket : uint8_t {
        DEPENDENT = 0,
        DEPENDENT_STLF,
        DEPENDENT_NON_STLF,
        DEPENDENT_LONG_LATENCY,
        DEPENDENT_NON_STLF_LONG_LATENCY,
        REGULAR,
        COUNT
    };
    static constexpr uint8_t kLoadBucketCount = static_cast<uint8_t>(LoadBucket::COUNT);

    struct LoadClassStats {
        uint64_t load_count = 0;
        uint64_t stlf_hits = 0;
        uint64_t l1_hits = 0;
        uint64_t l2_hits = 0; // Mapped to LLC in current model.
        uint64_t l3_hits = 0; // Kept for output compatibility; currently always 0.
        uint64_t dram_misses = 0;
        uint64_t total_latency = 0;
        uint64_t total_stall_cycles = 0;
        uint64_t long_latency_count = 0;
        uint64_t long_latency_total = 0;
    };

    struct LoadPcStats {
        uint64_t total_count = 0;
        uint64_t dependent_count = 0;
        uint64_t dependent_stlf_count = 0;
        uint64_t dependent_nonstlf_count = 0;
        uint64_t dependent_long_latency_count = 0;
        uint64_t dependent_nonstlf_long_latency_count = 0;
        uint64_t regular_count = 0;
        uint64_t total_latency = 0;
        uint64_t stlf_hits = 0;
        uint64_t l1_hits = 0;
        uint64_t l2_hits = 0;
        uint64_t l3_hits = 0;
        uint64_t dram_misses = 0;
    };

    std::vector<RegWriterInfo> reg_last_writer;
    std::vector<RegWriterInfo> reg_last_committed_writer;
    uint64_t prof_total_dynamic_loads = 0;
    uint64_t prof_dependent_loads = 0;
    uint64_t prof_regular_loads = 0;
    uint64_t prof_dependent_load_stlf_hits = 0;
    uint64_t prof_dependent_load_non_stlf = 0;
    uint64_t prof_long_latency_dependent_loads = 0;
    uint64_t prof_long_latency_dependent_loads_non_stlf = 0;
    std::array<LoadClassStats, kLoadBucketCount> prof_bucket_stats{};
    std::unordered_map<uint32_t, LoadPcStats> load_pc_stats;

    TraceSim(Ref_cpu &cpu, SimMode m, uint32_t w = TraceSimConfig::FETCH_WIDTH, 
             uint32_t rob_s = TraceSimConfig::ROB_SIZE, 
             uint32_t alu_iq_s = TraceSimConfig::ALU_IQ_SIZE,
             uint32_t ldu_iq_s = TraceSimConfig::LDU_IQ_SIZE,
             uint32_t sta_iq_s = TraceSimConfig::STA_IQ_SIZE,
             uint32_t std_iq_s = TraceSimConfig::STD_IQ_SIZE,
             uint32_t bru_iq_s = TraceSimConfig::BRU_IQ_SIZE,
             uint64_t max_insts = 0,
             bool enable_dep_load_profile = TraceSimConfig::ENABLE_DEPENDENT_LOAD_PROFILING)
        : ref_cpu(cpu), mode(m), width(w), rob_size(rob_s), 
          alu_iq_size(alu_iq_s), ldu_iq_size(ldu_iq_s), sta_iq_size(sta_iq_s), std_iq_size(std_iq_s),
          bru_iq_size(bru_iq_s),
          max_instructions(max_insts),
          enable_dependent_load_profiling(enable_dep_load_profile),
          icache(TraceSimConfig::ICACHE_SIZE, TraceSimConfig::ICACHE_ASSOC, TraceSimConfig::ICACHE_LINE_SIZE), 
          dcache(TraceSimConfig::DCACHE_SIZE, TraceSimConfig::DCACHE_ASSOC, TraceSimConfig::DCACHE_LINE_SIZE),
          llc(TraceSimConfig::LLC_SIZE, TraceSimConfig::LLC_ASSOC, TraceSimConfig::LLC_LINE_SIZE) {
        reg_ready_time.resize(32, 0);
        reg_last_writer.resize(32);
        reg_last_committed_writer.resize(32);
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
        uint64_t llc_access = 0;
        uint64_t llc_hit = 0;
    } cache_baselines;

    void reset_stats() {
        std::cout << "--- Warmup Finished, Capturing Statistics Baseline ---" << std::endl;
        inst_retired_baseline = instructions_retired;
        total_cycles_baseline = total_cycles;
        cache_baselines.icache_access = icache.access_count;
        cache_baselines.icache_hit = icache.hit_count;
        cache_baselines.dcache_access = dcache.access_count;
        cache_baselines.dcache_hit = dcache.hit_count;
        cache_baselines.llc_access = llc.access_count;
        cache_baselines.llc_hit = llc.hit_count;
        
        stats = {0, 0, 0, 0}; // These are fine to reset as they are additive only
        if (enable_dependent_load_profiling) {
            prof_total_dynamic_loads = 0;
            prof_dependent_loads = 0;
            prof_regular_loads = 0;
            prof_dependent_load_stlf_hits = 0;
            prof_dependent_load_non_stlf = 0;
            prof_long_latency_dependent_loads = 0;
            prof_long_latency_dependent_loads_non_stlf = 0;
            prof_bucket_stats.fill(LoadClassStats{});
            load_pc_stats.clear();
            std::fill(reg_last_writer.begin(), reg_last_writer.end(), RegWriterInfo{});
            std::fill(reg_last_committed_writer.begin(), reg_last_committed_writer.end(), RegWriterInfo{});
        }
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
        uint64_t rel_llc_access = llc.access_count - cache_baselines.llc_access;
        uint64_t rel_llc_hit = llc.hit_count - cache_baselines.llc_hit;

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

        uint64_t llc_misses = rel_llc_access - rel_llc_hit;
        double llc_hit_rate = rel_llc_access > 0 ? (double)rel_llc_hit * 100.0 / rel_llc_access : 0;
        std::cout << "LLC(shared): " << rel_llc_access << " accesses, Hit Rate: "
                  << std::fixed << std::setprecision(2) << llc_hit_rate << "%"
                  << " (MPKI: " << (double)llc_misses / inst_kilo << ")" << std::endl;
        print_dependent_load_profile(rel_inst);
        std::cout << "-----------------------------\n" << std::endl;
    }

    double safe_ratio(uint64_t x, uint64_t y) const {
        return y == 0 ? 0.0 : static_cast<double>(x) / static_cast<double>(y);
    }

    bool is_dependent_load(const OpEntry &op) const { return op.is_dependent_load; }
    bool is_stlf_hit(bool stlf_hit) const { return stlf_hit; }
    bool is_long_latency(uint32_t latency) const { return latency > TraceSimConfig::LONG_LATENCY_THRESHOLD; }

    std::array<bool, kLoadBucketCount> classify_load_bucket(bool dependent, bool stlf_hit, bool long_latency) const {
        std::array<bool, kLoadBucketCount> mark{};
        if (dependent) {
            mark[static_cast<uint8_t>(LoadBucket::DEPENDENT)] = true;
            if (stlf_hit) {
                mark[static_cast<uint8_t>(LoadBucket::DEPENDENT_STLF)] = true;
            } else {
                mark[static_cast<uint8_t>(LoadBucket::DEPENDENT_NON_STLF)] = true;
            }
            if (long_latency) {
                mark[static_cast<uint8_t>(LoadBucket::DEPENDENT_LONG_LATENCY)] = true;
                if (!stlf_hit) {
                    mark[static_cast<uint8_t>(LoadBucket::DEPENDENT_NON_STLF_LONG_LATENCY)] = true;
                }
            }
        } else {
            mark[static_cast<uint8_t>(LoadBucket::REGULAR)] = true;
        }
        return mark;
    }

    void record_bucket_stat(LoadBucket bucket, const OpEntry &op, uint32_t latency,
                            bool stlf_hit, bool l1_hit, bool l2_hit, bool dram_miss, bool long_latency) {
        LoadClassStats &bucket_stat = prof_bucket_stats[static_cast<uint8_t>(bucket)];
        bucket_stat.load_count++;
        if (stlf_hit) {
            bucket_stat.stlf_hits++;
        } else if (l1_hit) {
            bucket_stat.l1_hits++;
        } else if (l2_hit) {
            bucket_stat.l2_hits++;
        } else if (dram_miss) {
            bucket_stat.dram_misses++;
        }
        bucket_stat.total_latency += latency;
        uint64_t stall_cycles = (op.dispatch_cycle <= op.issue_cycle) ? (op.issue_cycle - op.dispatch_cycle) : 0;
        bucket_stat.total_stall_cycles += stall_cycles;
        if (long_latency) {
            bucket_stat.long_latency_count++;
            bucket_stat.long_latency_total += latency;
        }
    }

    void record_load_event(const OpEntry &op, bool dependent, bool stlf_hit, uint32_t latency,
                           bool l1_hit, bool l2_hit, bool dram_miss) {
        const bool long_latency = is_long_latency(latency);
        prof_total_dynamic_loads++;
        if (dependent) {
            prof_dependent_loads++;
            if (stlf_hit) {
                prof_dependent_load_stlf_hits++;
            } else {
                prof_dependent_load_non_stlf++;
            }
            if (long_latency) {
                prof_long_latency_dependent_loads++;
                if (!stlf_hit) {
                    prof_long_latency_dependent_loads_non_stlf++;
                }
            }
        } else {
            prof_regular_loads++;
        }

        const auto marks = classify_load_bucket(dependent, stlf_hit, long_latency);
        for (uint8_t i = 0; i < kLoadBucketCount; ++i) {
            if (marks[i]) {
                record_bucket_stat(static_cast<LoadBucket>(i), op, latency, stlf_hit, l1_hit, l2_hit, dram_miss, long_latency);
            }
        }

        LoadPcStats &pcs = load_pc_stats[op.inst.pc];
        pcs.total_count++;
        pcs.total_latency += latency;
        if (dependent) {
            pcs.dependent_count++;
            if (stlf_hit) {
                pcs.dependent_stlf_count++;
            } else {
                pcs.dependent_nonstlf_count++;
            }
            if (long_latency) {
                pcs.dependent_long_latency_count++;
                if (!stlf_hit) {
                    pcs.dependent_nonstlf_long_latency_count++;
                }
            }
        } else {
            pcs.regular_count++;
        }
        if (stlf_hit) {
            pcs.stlf_hits++;
        } else if (l1_hit) {
            pcs.l1_hits++;
        } else if (l2_hit) {
            pcs.l2_hits++;
        } else if (dram_miss) {
            pcs.dram_misses++;
        }
    }

    void print_load_stats_block(const char *name, const LoadClassStats &bucket) {
        std::cout << "\n[" << name << "]" << std::endl;
        std::cout << "load_count: " << bucket.load_count << std::endl;
        std::cout << "stlf_hits: " << bucket.stlf_hits << std::endl;
        std::cout << "l1_hits: " << bucket.l1_hits << std::endl;
        std::cout << "l2_hits: " << bucket.l2_hits << std::endl;
        std::cout << "l3_hits: " << bucket.l3_hits << " (no L3 model; reserved)" << std::endl;
        std::cout << "dram_misses: " << bucket.dram_misses << std::endl;
        std::cout << "total_load_latency: " << bucket.total_latency << std::endl;
        std::cout << "avg_load_latency: " << std::fixed << std::setprecision(4)
                  << (bucket.load_count ? static_cast<double>(bucket.total_latency) / static_cast<double>(bucket.load_count) : 0.0) << std::endl;
        std::cout << "total_stall_cycles: " << bucket.total_stall_cycles << std::endl;
        std::cout << "long_latency_threshold: " << TraceSimConfig::LONG_LATENCY_THRESHOLD << std::endl;
        std::cout << "long_latency_count: " << bucket.long_latency_count << std::endl;
        std::cout << "long_latency_total_latency: " << bucket.long_latency_total << std::endl;
    }

    void print_dependent_load_profile(uint64_t rel_inst) {
        if (!enable_dependent_load_profiling) {
            return;
        }

        std::cout << "\n[GLOBAL]" << std::endl;
        std::cout << "total_dynamic_insts: " << rel_inst << std::endl;
        std::cout << "total_dynamic_loads: " << prof_total_dynamic_loads << std::endl;
        std::cout << "total_dependent_loads: " << prof_dependent_loads << std::endl;
        std::cout << "dependent_loads: " << prof_dependent_loads << std::endl;
        std::cout << "regular_loads: " << prof_regular_loads << std::endl;
        std::cout << "dependent_load_stlf_hits: " << prof_dependent_load_stlf_hits << std::endl;
        std::cout << "dependent_load_non_stlf: " << prof_dependent_load_non_stlf << std::endl;
        std::cout << "long_latency_dependent_loads: " << prof_long_latency_dependent_loads << std::endl;
        std::cout << "long_latency_dependent_loads_non_stlf: " << prof_long_latency_dependent_loads_non_stlf << std::endl;
        std::cout << "dependent_ratio: " << std::fixed << std::setprecision(4)
                  << safe_ratio(prof_dependent_loads, prof_total_dynamic_loads) << std::endl;

        print_load_stats_block("DEPENDENT_LOAD", prof_bucket_stats[static_cast<uint8_t>(LoadBucket::DEPENDENT)]);
        print_load_stats_block("DEPENDENT_LOAD_STLF", prof_bucket_stats[static_cast<uint8_t>(LoadBucket::DEPENDENT_STLF)]);
        print_load_stats_block("DEPENDENT_LOAD_NON_STLF", prof_bucket_stats[static_cast<uint8_t>(LoadBucket::DEPENDENT_NON_STLF)]);
        print_load_stats_block("DEPENDENT_LOAD_LONG_LATENCY", prof_bucket_stats[static_cast<uint8_t>(LoadBucket::DEPENDENT_LONG_LATENCY)]);
        print_load_stats_block("DEPENDENT_LOAD_NON_STLF_LONG_LATENCY", prof_bucket_stats[static_cast<uint8_t>(LoadBucket::DEPENDENT_NON_STLF_LONG_LATENCY)]);
        print_load_stats_block("REGULAR_LOAD", prof_bucket_stats[static_cast<uint8_t>(LoadBucket::REGULAR)]);

        std::vector<std::pair<uint32_t, LoadPcStats>> pc_rows;
        pc_rows.reserve(load_pc_stats.size());
        for (const auto &kv : load_pc_stats) {
            pc_rows.push_back(kv);
        }
        std::sort(pc_rows.begin(), pc_rows.end(),
                  [](const auto &a, const auto &b) { return a.second.total_count > b.second.total_count; });

        const uint32_t top_n = std::min<uint32_t>(TraceSimConfig::DEPENDENT_LOAD_TOP_N, static_cast<uint32_t>(pc_rows.size()));
        std::cout << "\n[TOP_LOAD_PCS]" << std::endl;
        std::cout << "pc,total_count,dependent_count,dependent_stlf_count,dependent_nonstlf_count,dependent_long_latency_count,dependent_nonstlf_long_latency_count,regular_count,avg_latency,l1_hits,l2_hits,l3_hits,dram_misses" << std::endl;
        for (uint32_t i = 0; i < top_n; ++i) {
            const auto &row = pc_rows[i];
            const LoadPcStats &s = row.second;
            const double avg_lat = s.total_count ? static_cast<double>(s.total_latency) / static_cast<double>(s.total_count) : 0.0;
            std::cout << "0x" << std::hex << row.first << std::dec
                      << "," << s.total_count
                      << "," << s.dependent_count
                      << "," << s.dependent_stlf_count
                      << "," << s.dependent_nonstlf_count
                      << "," << s.dependent_long_latency_count
                      << "," << s.dependent_nonstlf_long_latency_count
                      << "," << s.regular_count
                      << "," << std::fixed << std::setprecision(4) << avg_lat
                      << "," << s.l1_hits
                      << "," << s.l2_hits
                      << "," << s.l3_hits
                      << "," << s.dram_misses
                      << std::endl;
        }
    }

    void run();
};
