#include "TraceSim.h"
#include <iomanip>

void TraceSim::run() {
    std::cout << "Starting Out-of-Order Trace-based Simulation..." << std::endl;
    std::cout << "Config: Width=" << width << ", ROB=" << rob_size 
              << ", ALU_IQ=" << alu_iq_size << ", LDU_IQ=" << ldu_iq_size 
              << ", STA_IQ=" << sta_iq_size << ", STD_IQ=" << std_iq_size 
              << ", BRU_IQ=" << bru_iq_size << std::endl;
    std::cout << "Dependent-load profiling: "
              << (enable_dependent_load_profiling ? "enabled" : "disabled")
              << std::endl;

    while (!ref_cpu.sim_end || rob_count > 0 || !fetch_queue.empty()) {
        if (in_warmup) {
            if (instructions_retired >= TraceSimConfig::WARMUP_INSTRUCTIONS) {
                std::cout << "\n--- WARMUP PHASE STATISTICS ---" << std::endl;
                print_stats();
                reset_stats();
                in_warmup = false;
            }
        } else {
            uint64_t rel_retired = instructions_retired - inst_retired_baseline;
            
            // Mode-specific termination
            if (mode == SimMode::RESTORE) {
                if (TraceSimConfig::SAMPLE_INSTRUCTIONS > 0 && rel_retired >= TraceSimConfig::SAMPLE_INSTRUCTIONS) {
                    std::cout << "CKPT mode: Sample instructions reached (" << TraceSimConfig::SAMPLE_INSTRUCTIONS << "). Ending simulation." << std::endl;
                    break;
                }
            } else {
                // TRACE mode: Use user-provided max_instructions
                if (max_instructions > 0 && instructions_retired >= max_instructions) {
                    std::cout << "TRACE mode: Max instructions reached (" << max_instructions << "). Ending simulation." << std::endl;
                    break;
                }
            }
        }
        
        // 1. Commit Stage
        for (uint32_t i = 0; i < width && rob_count > 0; ++i) {
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
                    if (!store_indices.empty()) {
                        store_indices.pop_front();
                    }
                }
                
                rob_head = (rob_head + 1) % rob_size;
                rob_count--;
                instructions_retired++;
                
                uint64_t rel_retired = instructions_retired - inst_retired_baseline;
                uint64_t rel_cycles = total_cycles - total_cycles_baseline;
                if (rel_retired % 10000000 == 0) {
                    std::cout << (in_warmup ? "[Warmup] " : "[Sample] ")
                              << "Retired: " << rel_retired 
                              << " Cycle: " << rel_cycles 
                              << " IPC: " << std::fixed << std::setprecision(2) 
                              << (rel_cycles > 0 ? (double)rel_retired / rel_cycles : 0) << std::endl;
                }
            } else {
                break; // Head not ready
            }
        }

        // 2. Issue Stage
        // -> ALU Issue
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

        // -> STA Issue (Store Address)
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

        // -> STD Issue (Store Data)
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

        // -> LDU Issue (Load Unit)
        uint32_t ldu_issued = 0;
        for (auto it = ldu_iq.begin(); it != ldu_iq.end() && ldu_issued < fu_width.ldu; ) {
            uint32_t rob_idx = *it;
            OpEntry &op = rob[rob_idx];
            if (reg_ready_time[op.inst.rs1] <= total_cycles) {
                if (enable_dependent_load_profiling && !op.load_classified) {
                    op.is_dependent_load = op.base_writer_valid && op.base_writer_is_load;
                    op.load_classified = true;
                }
                bool stall = false;
                OpEntry *stlf_src = nullptr;
                for (uint32_t st_idx : store_indices) {
                    OpEntry &st_op = rob[st_idx];
                    if (st_op.entry_id >= op.entry_id) break; // Reached current load or younger store

                    if (TraceSimConfig::MEM_DEP_MODEL == TraceSimConfig::MemDepModel::CONSERVATIVE_STA_VISIBLE) {
                        // Conservative model: unknown older store address blocks load.
                        if (!st_op.sta_done) {
                            stall = true;
                            stats.mem_dep_stalls++;
                            break;
                        }
                        if (st_op.inst.mem_addr == op.inst.mem_addr) {
                            stlf_src = &st_op; // Keep youngest matching older store seen so far.
                        }
                    } else {
                        // Oracle STLF model: address alias is known exactly even before STA.
                        if (st_op.inst.mem_addr == op.inst.mem_addr) {
                            stlf_src = &st_op; // Keep youngest matching older store.
                        }
                    }
                }

                if (!stall && stlf_src != nullptr) {
                    if (TraceSimConfig::ENABLE_STLF && stlf_src->std_done && total_cycles >= stlf_src->std_cycle) {
                        // STLF Hit
                        stats.stlf_hits++;
                        ldu_issued++;
                        op.issue_cycle = total_cycles;
                        op.execute_cycle = total_cycles + 1;
                        if (op.inst.rd != 0) reg_ready_time[op.inst.rd] = op.execute_cycle;
                        op.executed = true;
                        if (enable_dependent_load_profiling) {
                            record_load_event(
                                op,
                                is_dependent_load(op),
                                is_stlf_hit(true),
                                /*latency=*/1,
                                /*l1_hit=*/false,
                                /*l2_hit=*/false,
                                /*dram_miss=*/false);
                        }
                        it = ldu_iq.erase(it);
                        goto next_ldu;
                    }
                    stall = true; // True-alias store exists but data not ready (or STLF disabled).
                    stats.mem_dep_stalls++;
                }

                if (!stall) {
                    ldu_issued++;
                    op.issue_cycle = total_cycles;
                    bool l1_hit = dcache.access(op.inst.mem_addr, total_cycles);
                    bool l2_hit = false;
                    bool dram_miss = false;
                    uint32_t latency = TraceSimConfig::LDU_LATENCY;
                    if (!l1_hit) {
                        l2_hit = llc.access(op.inst.mem_addr, total_cycles);
                        latency += TraceSimConfig::LLC_HIT_LATENCY;
                        if (!l2_hit) {
                            dram_miss = true;
                            latency += TraceSimConfig::MEMORY_MISS_PENALTY;
                        }
                    }
                    
                    op.execute_cycle = total_cycles + latency;
                    if (op.inst.rd != 0) reg_ready_time[op.inst.rd] = op.execute_cycle;
                    op.executed = true;
                    if (enable_dependent_load_profiling) {
                        record_load_event(
                            op,
                            is_dependent_load(op),
                            is_stlf_hit(false),
                            latency,
                            l1_hit,
                            l2_hit,
                            dram_miss);
                    }
                    it = ldu_iq.erase(it);
                    continue;
                }
            }
            ++it;
            next_ldu:;
        }

        // -> BRU Issue
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

        // 3. Dispatch Stage
        for (uint32_t i = 0; i < width && !fetch_queue.empty(); ++i) {
            if (rob_count >= rob_size) break;
            
            OpEntry& op = fetch_queue.front();
            // Trap/MMIO/exception uop must serialize: wait until ROB drains.
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

            // Dispatch to ROB
            uint32_t rob_idx = rob_tail;
            rob[rob_idx] = op;
            rob[rob_idx].dispatch_cycle = total_cycles;
            rob[rob_idx].agu_done = false; 
            rob[rob_idx].sta_done = false;
            rob[rob_idx].std_done = false;
            rob[rob_idx].executed = false;
            rob[rob_idx].load_classified = false;
            rob[rob_idx].is_dependent_load = false;
            rob[rob_idx].base_writer_valid = false;
            rob[rob_idx].base_writer_is_load = false;
            rob[rob_idx].base_writer_entry_id = 0;
            rob[rob_idx].base_writer_pc = 0;

            if (enable_dependent_load_profiling && op.inst.is_load) {
                const uint32_t base_reg = op.inst.rs1;
                if (base_reg != 0) {
                    const RegWriterInfo &writer = reg_last_writer[base_reg];
                    rob[rob_idx].base_writer_valid = writer.valid;
                    rob[rob_idx].base_writer_is_load = writer.is_load;
                    rob[rob_idx].base_writer_entry_id = writer.entry_id;
                    rob[rob_idx].base_writer_pc = writer.pc;
                }
            }
            
            rob_tail = (rob_tail + 1) % rob_size;
            rob_count++;
            fetch_queue.pop_front();

            // Rename-style producer tracking: snapshot the latest dynamic writer
            // so younger ops can classify dependencies without changing pipeline behavior.
            if (enable_dependent_load_profiling && op.inst.rd != 0) {
                RegWriterInfo &dst_writer = reg_last_writer[op.inst.rd];
                dst_writer.valid = true;
                dst_writer.entry_id = op.entry_id;
                dst_writer.is_load = op.inst.is_load;
                dst_writer.pc = op.inst.pc;
            }
            
            if (op.inst.is_trap) {
                rob[rob_idx].fu_type = FU_Type::ALU;
                alu_iq.push_back(rob_idx);
            } else if (op.fu_type == FU_Type::ALU) {
                alu_iq.push_back(rob_idx);
            } else if (op.fu_type == FU_Type::BRU) {
                bru_iq.push_back(rob_idx);
            } else if (op.fu_type == FU_Type::LSU) {
                if (op.inst.is_load) {
                    ldu_iq.push_back(rob_idx);
                } else if (op.inst.is_store) {
                    sta_iq.push_back(rob_idx);
                    std_iq.push_back(rob_idx);
                    store_indices.push_back(rob_idx);
                } else {
                    // Trap-like or side-effect instructions can keep LSU opcode
                    // but not be an actual memory op; execute as ALU to avoid deadlock.
                    rob[rob_idx].fu_type = FU_Type::ALU;
                    alu_iq.push_back(rob_idx);
                }
            }
        }

        // 5. Fetch Stage
        if (total_cycles >= fetch_stall_until && fetch_queue.size() < width * 2) {
            uint32_t current_line_addr = 0;
            bool first_in_group = true;

            for (uint32_t i = 0; i < width; ++i) {
                if (ref_cpu.sim_end && pre_fetch_buffer.empty()) break;

                TraceInst inst;
                if (!pre_fetch_buffer.empty()) {
                    inst = pre_fetch_buffer.front();
                } else {
                    inst = ref_cpu.step();
                    if (inst.is_wfi || inst.is_ebreak) {
                        std::cout << (inst.is_wfi ? "WFI" : "EBREAK") 
                                  << " encountered at PC: 0x" << std::hex << inst.pc << std::dec << std::endl;
                        ref_cpu.sim_end = true;
                        break;
                    }
                }

                // Check Cache Line boundary + I-Cache behavior.
                if (TraceSimConfig::IGNORE_FETCH_BUBBLES) {
                    if (!icache.access(inst.pc, total_cycles)) {
                        bool llc_hit = llc.access(inst.pc, total_cycles);
                        uint32_t miss_penalty = TraceSimConfig::LLC_HIT_LATENCY;
                        if (!llc_hit) {
                            miss_penalty += TraceSimConfig::MEMORY_MISS_PENALTY;
                        }
                        fetch_stall_until = total_cycles + miss_penalty;
                        if (pre_fetch_buffer.empty()) pre_fetch_buffer.push_back(inst);
                        break;
                    }
                } else {
                    uint32_t inst_line = inst.pc & ~(icache.line_size - 1);
                    if (first_in_group) {
                        current_line_addr = inst_line;
                        // I-Cache simulation (only once per group/line)
                        if (!icache.access(inst.pc, total_cycles)) {
                            bool llc_hit = llc.access(inst.pc, total_cycles);
                            uint32_t miss_penalty = TraceSimConfig::LLC_HIT_LATENCY;
                            if (!llc_hit) {
                                miss_penalty += TraceSimConfig::MEMORY_MISS_PENALTY;
                            }
                            fetch_stall_until = total_cycles + miss_penalty;
                            if (pre_fetch_buffer.empty()) pre_fetch_buffer.push_back(inst);
                            break;
                        }
                        first_in_group = false;
                    } else if (inst_line != current_line_addr) {
                        // Boundary crossed - defer this instruction to next cycle
                        if (pre_fetch_buffer.empty()) pre_fetch_buffer.push_back(inst);
                        break;
                    }
                }

                // Actually fetch the instruction
                if (!pre_fetch_buffer.empty()) pre_fetch_buffer.pop_front();

                OpEntry op;
                op.inst = inst;
                op.entry_id = next_entry_id++;
                op.fetch_cycle = total_cycles;
                op.executed = false;
                op.fu_type = get_fu_type(inst.opcode);
                
                bool stop_fetch = false;
                if (inst.is_trap) {
                    stop_fetch = true;
                }
                if (inst.is_branch) {
                    stats.total_branches++;
                    bool pred = bp->predict(inst.pc, inst.branch_taken);
                    bp->update(inst.pc, inst.branch_taken);
                    if (pred != inst.branch_taken) {
                        fetch_stall_until = total_cycles + TraceSimConfig::BRANCH_MISPREDICT_PENALTY;
                        if (!TraceSimConfig::IGNORE_FETCH_BUBBLES) {
                            stop_fetch = true;
                        }
                    } else {
                        stats.correct_branches++;
                        if (inst.branch_taken && !TraceSimConfig::IGNORE_FETCH_BUBBLES) {
                            stop_fetch = true;
                        }
                    }
                }

                // If the instruction is a jump (JAL/JALR), always stop fetching this cycle
                uint32_t opcode = inst.opcode;
                if (!TraceSimConfig::IGNORE_FETCH_BUBBLES &&
                    (opcode == 0b1101111 || opcode == 0b1100111)) {
                    stop_fetch = true;
                }

                fetch_queue.push_back(op);
                if (stop_fetch) break;
            }
        }

        total_cycles++;
        if (total_cycles > MAX_SIM_TIME) break;
    }

    print_stats();
}
