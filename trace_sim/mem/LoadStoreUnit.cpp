#include "LoadStoreUnit.h"
#include "../TraceSim.h"
#include <iomanip>

void LoadStoreUnit::finalize_load_profile(OpEntry &op, uint32_t latency, bool l1_hit, bool l2_hit, bool dram_miss, bool is_stlf_hit) {
    if (sim.enable_dependent_load_profiling) {
        sim.record_load_event(op, op.is_dependent_load, is_stlf_hit, latency, l1_hit, l2_hit, dram_miss);
    }
}

LoadStoreUnit::StoreDepResult LoadStoreUnit::check_store_dependency(OpEntry &op) {
    bool stall = false;
    OpEntry *stlf_src = nullptr;
    for (uint32_t st_idx : sim.store_indices) {
        OpEntry &st_op = sim.rob[st_idx];
        if (st_op.entry_id >= op.entry_id) break;

        if (TraceSimConfig::MEM_DEP_MODEL == TraceSimConfig::MemDepModel::CONSERVATIVE_STA_VISIBLE) {
            if (!st_op.sta_done) {
                stall = true;
                op.waiting_on_mem_dep = true;
                sim.stats.mem_dep_stalls++;
                break;
            }
            if (st_op.inst.mem_addr == op.inst.mem_addr) {
                stlf_src = &st_op;
            }
        } else {
            if (st_op.inst.mem_addr == op.inst.mem_addr) {
                stlf_src = &st_op;
            }
        }
    }

    if (stall) {
        return StoreDepResult::Stalled;
    }

    if (stlf_src != nullptr) {
        if (TraceSimConfig::ENABLE_STLF && stlf_src->std_done && sim.total_cycles >= stlf_src->std_cycle) {
            return StoreDepResult::STLFReady;
        }
        op.waiting_on_mem_dep = true;
        sim.stats.mem_dep_stalls++;
        return StoreDepResult::Stalled;
    }
    return StoreDepResult::NoDep;
}

LoadStoreUnit::LoadIssueResult LoadStoreUnit::execute_load_memory_access(OpEntry &op) {
    Cache::AccessResult l1_res = sim.dcache.request(op.inst.mem_addr, sim.total_cycles, false);
    if (l1_res.status == Cache::AccessStatus::MISS_BLOCKED) {
        op.waiting_on_memory = true;
        return LoadIssueResult::StalledByMSHR;
    }

    op.issue_cycle = sim.total_cycles;
    const bool l1_hit = l1_res.status == Cache::AccessStatus::HIT;
    bool l2_hit = false;
    bool dram_miss = false;
    uint64_t ready_cycle = sim.total_cycles + TraceSimConfig::LDU_LATENCY;
    op.waiting_on_memory = !l1_hit;

    if (!l1_hit) {
        if (l1_res.status == Cache::AccessStatus::MISS_NEW) {
            Cache::AccessResult llc_res = sim.llc.request(op.inst.mem_addr, sim.total_cycles, false);
            if (llc_res.status == Cache::AccessStatus::MISS_BLOCKED) {
                op.waiting_on_memory = true;
                return LoadIssueResult::StalledByMSHR;
            }
            l2_hit = llc_res.status == Cache::AccessStatus::HIT;
            dram_miss = llc_res.status == Cache::AccessStatus::MISS_NEW;
            const uint64_t llc_ready = l2_hit
                                       ? sim.total_cycles + TraceSimConfig::LLC_HIT_LATENCY
                                       : (llc_res.status == Cache::AccessStatus::MISS_MERGED
                                              ? llc_res.ready_cycle
                                              : sim.total_cycles + TraceSimConfig::LLC_HIT_LATENCY + TraceSimConfig::MEMORY_MISS_PENALTY);
            if (dram_miss) {
                sim.llc.schedule_fill(op.inst.mem_addr, llc_ready, false);
            }
            ready_cycle = sim.total_cycles + TraceSimConfig::LDU_LATENCY + (llc_ready - sim.total_cycles);
            sim.dcache.schedule_fill(op.inst.mem_addr, ready_cycle, false);
        } else {
            ready_cycle = std::max(sim.total_cycles + static_cast<uint64_t>(TraceSimConfig::LDU_LATENCY), l1_res.ready_cycle);
        }
    }

    const uint32_t latency = static_cast<uint32_t>(ready_cycle - sim.total_cycles);
    op.execute_cycle = sim.total_cycles + latency;
    if (op.inst.rd != 0) sim.reg_ready_time[op.inst.rd] = op.execute_cycle;
    op.executed = true;

    finalize_load_profile(op, latency, l1_hit, l2_hit, dram_miss, false);
    sim.enqueue_prefetches(sim.dcache, op.inst.pc, op.inst.mem_addr, l1_hit, true);

    return LoadIssueResult::Issued;
}


void LoadStoreUnit::issue_stage() {
    uint32_t ldu_issued = 0;
    for (auto it = sim.ldu_iq.begin(); it != sim.ldu_iq.end() && ldu_issued < sim.fu_width.ldu; ) {
        LoadIssueResult res = try_issue_load(*it);
        if (res == LoadIssueResult::Issued || res == LoadIssueResult::STLFHit) {
            ldu_issued++;
            it = sim.ldu_iq.erase(it);
        } else {
            ++it;
        }
    }
}

LoadStoreUnit::LoadIssueResult LoadStoreUnit::try_issue_load(uint32_t rob_idx) {
    OpEntry &op = sim.rob[rob_idx];
    if (sim.reg_ready_time[op.inst.rs1] > sim.total_cycles) {
        return LoadIssueResult::NotReady;
    }

    op.waiting_on_mem_dep = false;
    if (sim.enable_dependent_load_profiling && !op.load_classified) {
        op.is_dependent_load = op.base_writer_valid && op.base_writer_is_load;
        op.load_classified = true;
    }

    StoreDepResult dep_res = check_store_dependency(op);
    if (dep_res == StoreDepResult::Stalled) {
        return LoadIssueResult::StalledByDep;
    }

    if (dep_res == StoreDepResult::STLFReady) {
        sim.stats.stlf_hits++;
        op.issue_cycle = sim.total_cycles;
        op.execute_cycle = sim.total_cycles + 1;
        if (op.inst.rd != 0) sim.reg_ready_time[op.inst.rd] = op.execute_cycle;
        op.executed = true;
        
        finalize_load_profile(op, 1, false, false, false, true);
        return LoadIssueResult::STLFHit;
    }

    return execute_load_memory_access(op);
}
