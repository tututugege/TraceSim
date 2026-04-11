#include "Frontend.h"
#include "../TraceSim.h"
#include "../mem/MemSubsystem.h"
#include <iostream>

void Frontend::fetch_stage() {
    BoundReason next_frontend_bound_hint = BoundReason::NONE;
    if (sim.frontend_can_fetch()) {
        uint32_t current_line_addr = 0;
        bool first_in_group = true;

        for (uint32_t i = 0; i < sim.fetch_bandwidth; ++i) {
            if (!sim.frontend_can_fetch()) {
                break;
            }
            if (sim.frontend_trace_exhausted()) {
                break;
            }

            TraceInst inst;
            if (!sim.frontend_acquire_trace_inst(inst)) {
                break;
            }

            // Check cache-line boundary and model I-cache access once per line.
            uint32_t inst_line = inst.pc & ~(sim.icache_line_size() - 1);
            if (first_in_group) {
                current_line_addr = inst_line;
                const auto ic_res = sim.mem->access_icache(inst.pc, sim.total_cycles);
                if (ic_res.blocked) {
                    next_frontend_bound_hint = BoundReason::ICACHE_MISS;
                    sim.frontend_requeue_trace_inst(inst);
                    break;
                }
                if (!ic_res.l1_hit) {
                    sim.set_fetch_stall(ic_res.ready_cycle,
                                        FetchStallReason::ICACHE_MISS);
                    sim.frontend_requeue_trace_inst(inst);
                    break;
                }
                sim.mem->enqueue_prefetch(false, inst.pc, inst.pc, true);
                first_in_group = false;
            } else if (inst_line != current_line_addr) {
                // Boundary crossed - defer this instruction to next cycle.
                next_frontend_bound_hint = BoundReason::LINE_BOUNDARY;
                sim.set_fetch_stall(sim.total_cycles + 1, FetchStallReason::LINE_BOUNDARY);
                sim.frontend_requeue_trace_inst(inst);
                break;
            }

            OpEntry op;
            op.inst = inst;
            op.entry_id = sim.next_entry_id++;
            op.fetch_cycle = sim.total_cycles;
            op.executed = false;
            op.fu_type = sim.get_fu_type(inst.opcode);
            
            bool stop_fetch = false;
            BoundReason fetch_stop_reason = BoundReason::NONE;
            
            if (inst.is_trap) {
                stop_fetch = true;
            }
            if (inst.is_branch) {
                sim.profiler->inc_total_branches();
                bool pred_taken = false;
                if (branch_predictor_uses_oracle(TraceSimConfig::BP_TYPE)) {
                    pred_taken =
                        oracle_branch_prediction(TraceSimConfig::BP_TYPE,
                                                 TraceSimConfig::BP_TARGET_ACCURACY,
                                                 inst.branch_taken);
                } else {
                    const BranchPredictor::Prediction pred =
                        sim.bp->predict(inst.pc);
                    pred_taken = pred.taken;
                    sim.bp->update(inst.pc, inst.branch_taken, pred.meta);
                }
                if (pred_taken != inst.branch_taken) {
                    sim.set_fetch_stall(
                        sim.total_cycles + TraceSimConfig::BRANCH_MISPREDICT_PENALTY,
                        FetchStallReason::BRANCH_MISPREDICT);
                    stop_fetch = true;
                } else {
                    sim.profiler->inc_correct_branches();
                    if (inst.branch_taken) {
                        // 正确预测的跳转，仅需 1 周期重定向延迟 (FETCH_REDIRECT_LATENCY)
                        if (TraceSimConfig::FETCH_REDIRECT_LATENCY > 0) {
                            sim.set_fetch_stall(
                                sim.total_cycles + TraceSimConfig::FETCH_REDIRECT_LATENCY,
                                FetchStallReason::FETCH_REDIRECT);
                        }
                        stop_fetch = true;
                        fetch_stop_reason = BoundReason::FETCH_REDIRECT;
                    }
                }
            }

            // Jumps always redirect the fetch stream.
            uint32_t opcode = inst.opcode;
            if (opcode == 0b1101111 || opcode == 0b1100111) {
                if (TraceSimConfig::FETCH_REDIRECT_LATENCY > 0) {
                    sim.set_fetch_stall(
                        sim.total_cycles + TraceSimConfig::FETCH_REDIRECT_LATENCY,
                        FetchStallReason::FETCH_REDIRECT);
                }
                stop_fetch = true;
                fetch_stop_reason = BoundReason::FETCH_REDIRECT;
            }

            sim.push_inst_buffer(op);
            if (stop_fetch) {
                if (fetch_stop_reason != BoundReason::NONE) {
                    next_frontend_bound_hint = fetch_stop_reason;
                }
                break;
            }
        }
    }
    sim.set_frontend_bound_hint(next_frontend_bound_hint);
}
