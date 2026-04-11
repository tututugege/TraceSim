#include "Frontend.h"
#include "../TraceSim.h"
#include <iostream>

void Frontend::fetch_stage() {
    BoundReason next_frontend_bound_hint = BoundReason::NONE;
    if (sim.total_cycles >= sim.fetch_stall_until &&
        sim.inst_buffer.size() < sim.inst_buffer_size) {
        uint32_t current_line_addr = 0;
        bool first_in_group = true;

        for (uint32_t i = 0; i < sim.fetch_bandwidth; ++i) {
            if (sim.inst_buffer.size() >= sim.inst_buffer_size) {
                break;
            }
            if (sim.ref_cpu.sim_end && sim.pre_fetch_buffer.empty()) break;

            TraceInst inst;
            if (!sim.pre_fetch_buffer.empty()) {
                inst = sim.pre_fetch_buffer.front();
            } else {
                inst = sim.ref_cpu.step();
                if (inst.is_wfi || inst.is_ebreak) {
                    std::cout << (inst.is_wfi ? "WFI" : "EBREAK") 
                              << " encountered at PC: 0x" << std::hex << inst.pc << std::dec << std::endl;
                    sim.ref_cpu.sim_end = true;
                    break;
                }
            }

            // Check cache-line boundary and model I-cache access once per line.
            uint32_t inst_line = inst.pc & ~(sim.icache.line_size - 1);
            if (first_in_group) {
                current_line_addr = inst_line;
                Cache::AccessResult ic_res =
                    sim.icache.request(inst.pc, sim.total_cycles, false);
                if (ic_res.status == Cache::AccessStatus::MISS_BLOCKED) {
                    next_frontend_bound_hint = BoundReason::ICACHE_MISS;
                    if (sim.pre_fetch_buffer.empty()) sim.pre_fetch_buffer.push_back(inst);
                    break;
                }
                const bool ic_hit = ic_res.status == Cache::AccessStatus::HIT;
                if (!ic_hit) {
                    uint64_t ready_cycle =
                        std::max(sim.total_cycles, ic_res.ready_cycle);
                    if (ic_res.status == Cache::AccessStatus::MISS_NEW) {
                        Cache::AccessResult llc_res =
                            sim.llc.request(inst.pc, sim.total_cycles, false);
                        if (llc_res.status == Cache::AccessStatus::MISS_BLOCKED) {
                            next_frontend_bound_hint = BoundReason::ICACHE_MISS;
                            if (sim.pre_fetch_buffer.empty()) sim.pre_fetch_buffer.push_back(inst);
                            break;
                        }
                        const bool llc_hit = llc_res.status == Cache::AccessStatus::HIT;
                        ready_cycle = llc_hit
                                ? sim.total_cycles + TraceSimConfig::LLC_HIT_LATENCY
                                : (llc_res.status == Cache::AccessStatus::MISS_MERGED
                                       ? llc_res.ready_cycle
                                       : sim.total_cycles + TraceSimConfig::LLC_HIT_LATENCY + TraceSimConfig::MEMORY_MISS_PENALTY);
                        if (!llc_hit && llc_res.status == Cache::AccessStatus::MISS_NEW) {
                            sim.llc.schedule_fill(inst.pc, ready_cycle, false);
                        }
                        sim.icache.schedule_fill(inst.pc, ready_cycle, false);
                    }
                    sim.set_fetch_stall(ready_cycle, FetchStallReason::ICACHE_MISS);
                    if (sim.pre_fetch_buffer.empty()) sim.pre_fetch_buffer.push_back(inst);
                    break;
                }
                sim.enqueue_prefetches(sim.icache, inst.pc, inst.pc, true, false);
                first_in_group = false;
            } else if (inst_line != current_line_addr) {
                // Boundary crossed - defer this instruction to next cycle.
                next_frontend_bound_hint = BoundReason::LINE_BOUNDARY;
                sim.set_fetch_stall(sim.total_cycles + 1, FetchStallReason::LINE_BOUNDARY);
                if (sim.pre_fetch_buffer.empty()) sim.pre_fetch_buffer.push_back(inst);
                break;
            }

            // Actually fetch the instruction
            if (!sim.pre_fetch_buffer.empty()) sim.pre_fetch_buffer.pop_front();

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
                bool pred = sim.bp->predict(inst.pc, inst.branch_taken);
                sim.bp->update(inst.pc, inst.branch_taken);
                if (pred != inst.branch_taken) {
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

            sim.inst_buffer.push_back(op);
            if (stop_fetch) {
                if (fetch_stop_reason != BoundReason::NONE) {
                    next_frontend_bound_hint = fetch_stop_reason;
                }
                break;
            }
        }
    }
    sim.frontend_bound_hint = next_frontend_bound_hint;
}
