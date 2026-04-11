#include "TraceSim.h"
#include "frontend/Frontend.h"
#include "mem/LoadStoreUnit.h"
#include <iomanip>

TraceSim::TraceSim(Ref_cpu &cpu, SimMode m, uint32_t w, 
                   uint32_t rob_s, uint32_t alu_iq_s, uint32_t ldu_iq_s,
                   uint32_t sta_iq_size, uint32_t std_iq_size, uint32_t bru_iq_size,
                   uint64_t max_insts, bool enable_dep_load_profile)
    : ref_cpu(cpu), mode(m), fetch_bandwidth(w), width(w), rob_size(rob_s), 
      alu_iq_size(alu_iq_s), ldu_iq_size(ldu_iq_s), sta_iq_size(sta_iq_size), std_iq_size(std_iq_size),
      bru_iq_size(bru_iq_size),
      max_instructions(max_insts),
      enable_dependent_load_profiling(enable_dep_load_profile),
      icache(TraceSimConfig::ICACHE_SIZE, TraceSimConfig::ICACHE_ASSOC, TraceSimConfig::ICACHE_LINE_SIZE), 
      dcache(TraceSimConfig::DCACHE_SIZE, TraceSimConfig::DCACHE_ASSOC, TraceSimConfig::DCACHE_LINE_SIZE),
      llc(TraceSimConfig::LLC_SIZE, TraceSimConfig::LLC_ASSOC, TraceSimConfig::LLC_LINE_SIZE),
      frontend(std::make_unique<Frontend>(*this)),
      lsu(std::make_unique<LoadStoreUnit>(*this)) {
    
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
    alu_iq.reserve(alu_iq_size);
    ldu_iq.reserve(ldu_iq_size);
    sta_iq.reserve(sta_iq_size);
    std_iq.reserve(std_iq_size);
    bru_iq.reserve(bru_iq_size);
}

TraceSim::~TraceSim() = default;

void TraceSim::reset_stats() {
    inst_retired_baseline = instructions_retired;
    total_cycles_baseline = total_cycles;
    cache_baselines.icache_access = icache.access_count;
    cache_baselines.icache_hit = icache.hit_count;
    cache_baselines.dcache_access = dcache.access_count;
    cache_baselines.dcache_hit = dcache.hit_count;
    cache_baselines.llc_access = llc.access_count;
    cache_baselines.llc_hit = llc.hit_count;
    stats = {0, 0, 0, 0};
}

void TraceSim::run() {
    std::cout << "Starting Out-of-Order Trace-based Simulation..." << std::endl;
    std::cout << "Config: Width=" << fetch_bandwidth << ", ROB=" << rob_size 
              << ", ALU_IQ=" << alu_iq_size << ", LDU_IQ=" << ldu_iq_size 
              << ", STA_IQ=" << sta_iq_size << ", STD_IQ=" << std_iq_size 
              << ", BRU_IQ=" << bru_iq_size << std::endl;

    while (!ref_cpu.sim_end || rob_count > 0 || !fetch_queue.empty() || !inst_buffer.empty()) {
        process_cache_returns();
        advance_cycle();

        commit_stage();
        issue_stage();
        dispatch_stage();
        decode_stage();
        frontend->fetch_stage();

        total_cycles++;
        if (total_cycles > MAX_SIM_TIME) {
            std::cout << "Simulation timeout reached!" << std::endl;
            break;
        }
    }

    print_stats();
}

void TraceSim::advance_cycle() {
    if (in_warmup) {
        if (instructions_retired >= TraceSimConfig::WARMUP_INSTRUCTIONS) {
            std::cout << "\n--- WARMUP PHASE STATISTICS ---" << std::endl;
            print_stats();
            reset_stats();
            in_warmup = false;
        }
    } else {
        uint64_t rel_retired = instructions_retired - inst_retired_baseline;
        if (mode == SimMode::RESTORE) {
            if (TraceSimConfig::SAMPLE_INSTRUCTIONS > 0 && rel_retired >= TraceSimConfig::SAMPLE_INSTRUCTIONS) {
                ref_cpu.sim_end = true;
            }
        } else {
            if (max_instructions > 0 && instructions_retired >= max_instructions) {
                ref_cpu.sim_end = true;
            }
        }
    }
}

void TraceSim::commit_stage() {
    for (uint32_t i = 0; i < fetch_bandwidth && rob_count > 0; ++i) {
        OpEntry &head_op = rob[rob_head];
        if (head_op.executed && head_op.execute_cycle <= total_cycles) {
            if (enable_dependent_load_profiling && head_op.inst.rd != 0) {
                RegWriterInfo &dst_writer = reg_last_committed_writer[head_op.inst.rd];
                dst_writer.valid = true;
                dst_writer.entry_id = head_op.entry_id;
                dst_writer.is_load = head_op.inst.is_load;
                dst_writer.pc = head_op.inst.pc;
            }
            if (head_op.inst.is_store) {
                if (!store_indices.empty()) store_indices.pop_front();
            }
            
            rob_head = (rob_head + 1) % rob_size;
            rob_count--;
            instructions_retired++;
            
            uint64_t rel_retired = instructions_retired - inst_retired_baseline;
            uint64_t rel_cycles = total_cycles - total_cycles_baseline;
            if (rel_retired > 0 && rel_retired % 10000000 == 0) {
                std::cout << (in_warmup ? "[Warmup] " : "[Sample] ")
                          << "Retired: " << rel_retired 
                          << " Cycle: " << rel_cycles 
                          << " IPC: " << std::fixed << std::setprecision(2) 
                          << (double)rel_retired / rel_cycles << std::endl;
            }
        } else {
            break;
        }
    }
}

void TraceSim::issue_stage() {
    // 1. ALU Issue
    uint32_t alu_issued = 0;
    for (auto it = alu_iq.begin(); it != alu_iq.end() && alu_issued < fu_width.alu; ) {
        OpEntry &op = rob[*it];
        if (reg_ready_time[op.inst.rs1] <= total_cycles && reg_ready_time[op.inst.rs2] <= total_cycles) {
            alu_issued++;
            op.issue_cycle = total_cycles;
            op.execute_cycle = total_cycles + TraceSimConfig::ALU_LATENCY;
            if (op.inst.rd != 0) reg_ready_time[op.inst.rd] = op.execute_cycle;
            op.executed = true; 
            it = alu_iq.erase(it);
        } else {
            ++it;
        }
    }

    // 2. LSU Issue
    lsu->issue_stage();

    // 3. STA/STD Issue (for Stores)
    uint32_t sta_issued = 0;
    for (auto it = sta_iq.begin(); it != sta_iq.end() && sta_issued < fu_width.sta; ) {
        OpEntry &op = rob[*it];
        if (reg_ready_time[op.inst.rs1] <= total_cycles) {
            sta_issued++;
            op.sta_cycle = total_cycles + TraceSimConfig::STA_LATENCY;
            op.sta_done = true;
            op.agu_done = true; 
            if (op.std_done) {
                op.execute_cycle = std::max(op.sta_cycle, op.std_cycle);
                op.executed = true;
            }
            it = sta_iq.erase(it);
        } else {
            ++it;
        }
    }

    uint32_t std_issued = 0;
    for (auto it = std_iq.begin(); it != std_iq.end() && std_issued < fu_width.std; ) {
        OpEntry &op = rob[*it];
        if (reg_ready_time[op.inst.rs2] <= total_cycles) {
            std_issued++;
            op.std_cycle = total_cycles + TraceSimConfig::STD_LATENCY;
            op.std_done = true;
            if (op.sta_done) {
                op.execute_cycle = std::max(op.sta_cycle, op.std_cycle);
                op.executed = true;
            }
            it = std_iq.erase(it);
        } else {
            ++it;
        }
    }

    // 4. BRU Issue
    uint32_t bru_issued = 0;
    for (auto it = bru_iq.begin(); it != bru_iq.end() && bru_issued < fu_width.bru; ) {
        OpEntry &op = rob[*it];
        if (reg_ready_time[op.inst.rs1] <= total_cycles && reg_ready_time[op.inst.rs2] <= total_cycles) {
            bru_issued++;
            op.issue_cycle = total_cycles;
            op.execute_cycle = total_cycles + TraceSimConfig::BRU_LATENCY;
            if (op.inst.rd != 0) reg_ready_time[op.inst.rd] = op.execute_cycle;
            op.executed = true; 
            it = bru_iq.erase(it);
        } else {
            ++it;
        }
    }
}

void TraceSim::dispatch_stage() {
    for (uint32_t i = 0; i < fetch_bandwidth && !fetch_queue.empty(); ++i) {
        if (rob_count >= rob_size) break;
        
        OpEntry& op = fetch_queue.front();
        if (op.inst.is_trap && rob_count > 0) break;
        
        bool iq_full = false;
        if (op.fu_type == FU_Type::ALU && alu_iq.size() >= alu_iq_size) iq_full = true;
        else if (op.fu_type == FU_Type::BRU && bru_iq.size() >= bru_iq_size) iq_full = true;
        else if (op.fu_type == FU_Type::LSU) {
            if (op.inst.is_load && ldu_iq.size() >= ldu_iq_size) iq_full = true;
            else if (op.inst.is_store && (sta_iq.size() >= sta_iq_size || std_iq.size() >= std_iq_size)) iq_full = true;
            else if (!op.inst.is_load && !op.inst.is_store && alu_iq.size() >= alu_iq_size) iq_full = true;
        }
        
        if (iq_full) break;

        uint32_t rob_idx = rob_tail;
        rob[rob_idx] = op;
        rob[rob_idx].dispatch_cycle = total_cycles;
        rob[rob_idx].executed = false;

        if (enable_dependent_load_profiling && op.inst.rd != 0) {
            RegWriterInfo &dst_writer = reg_last_writer[op.inst.rd];
            dst_writer.valid = true;
            dst_writer.entry_id = op.entry_id;
            dst_writer.is_load = op.inst.is_load;
            dst_writer.pc = op.inst.pc;
        }
        
        rob_tail = (rob_tail + 1) % rob_size;
        rob_count++;
        fetch_queue.pop_front();

        // Push to IQ
        if (op.inst.is_trap || op.fu_type == FU_Type::ALU) alu_iq.push_back(rob_idx);
        else if (op.fu_type == FU_Type::BRU) bru_iq.push_back(rob_idx);
        else if (op.fu_type == FU_Type::LSU) {
            if (op.inst.is_load) ldu_iq.push_back(rob_idx);
            else if (op.inst.is_store) {
                sta_iq.push_back(rob_idx);
                std_iq.push_back(rob_idx);
                store_indices.push_back(rob_idx);
            } else {
                rob[rob_idx].fu_type = FU_Type::ALU;
                alu_iq.push_back(rob_idx);
            }
        }
    }
}

void TraceSim::decode_stage() {
    // Connection: inst_buffer -> fetch_queue
    while (!inst_buffer.empty() && fetch_queue.size() < fetch_bandwidth * 2) {
        fetch_queue.push_back(inst_buffer.front());
        inst_buffer.pop_front();
    }
}

void TraceSim::print_stats() {
    uint64_t rel_inst = instructions_retired - inst_retired_baseline;
    uint64_t rel_cycles = total_cycles - total_cycles_baseline;
    
    std::cout << "\n--- Simulation Statistics ---" << std::endl;
    std::cout << "Total Instructions: " << rel_inst << std::endl;
    std::cout << "Total Cycles: " << rel_cycles << std::endl;
    if (rel_cycles > 0) {
        std::cout << "Overall IPC: " << std::fixed << std::setprecision(2) 
                  << (double)rel_inst / rel_cycles << std::endl;
    }
    std::cout << "STLF Hits: " << stats.stlf_hits << std::endl;
    std::cout << "Memory Dependency Stalls: " << stats.mem_dep_stalls << "\n" << std::endl;
}

void TraceSim::record_load_event(OpEntry&, bool, bool, uint32_t, bool, bool, bool) {}
void TraceSim::enqueue_prefetches(Cache &, uint32_t, uint32_t, bool, bool) {}
void TraceSim::service_prefetch_queues() {}
