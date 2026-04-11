#include "LoadStoreUnit.h"
#include "../TraceSim.h"
#include "MemSubsystem.h"
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
                sim.mark_waiting_on_mem_dep(op);
                sim.profiler->inc_mem_dep_stalls();
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
        sim.mark_waiting_on_mem_dep(op);
        sim.profiler->inc_mem_dep_stalls();
        return StoreDepResult::Stalled;
    }
    return StoreDepResult::NoDep;
}

LoadStoreUnit::LoadIssueResult LoadStoreUnit::execute_load_memory_access(OpEntry &op) {
    const auto mem_res =
        sim.mem->access_dcache_load(op.inst.mem_addr, sim.total_cycles);
    if (mem_res.blocked) {
        sim.mark_waiting_on_memory(op);
        return LoadIssueResult::StalledByMSHR;
    }

    const bool l1_hit = mem_res.l1_hit;
    const bool l2_hit = mem_res.llc_hit;
    const bool dram_miss = mem_res.dram_miss;
    const uint64_t ready_cycle = mem_res.ready_cycle;
    op.waiting_on_memory = !l1_hit;
    const uint32_t latency = static_cast<uint32_t>(ready_cycle - sim.total_cycles);
    sim.mark_load_executed(op, ready_cycle);

    finalize_load_profile(op, latency, l1_hit, l2_hit, dram_miss, false);
    sim.mem->enqueue_prefetch(true, op.inst.pc, op.inst.mem_addr, l1_hit);

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
    if (!sim.is_register_ready(op.inst.rs1)) {
        return LoadIssueResult::NotReady;
    }

    sim.clear_load_memory_flags(op);
    sim.classify_load_if_needed(op);

    StoreDepResult dep_res = check_store_dependency(op);
    if (dep_res == StoreDepResult::Stalled) {
        return LoadIssueResult::StalledByDep;
    }

    if (dep_res == StoreDepResult::STLFReady) {
        sim.profiler->inc_stlf_hits();
        sim.mark_load_executed(op, sim.total_cycles + 1);
        finalize_load_profile(op, 1, false, false, false, true);
        return LoadIssueResult::STLFHit;
    }

    return execute_load_memory_access(op);
}
