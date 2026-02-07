#include "CSR.h"
#include "RISCV.h"
#include "ref.h"
#include <cstdint>
#include <iostream>
#include <map>

// Removed load_simpoints declaration

void Ref_cpu::init(uint32_t reset_pc, const char *image, uint32_t size) {
  state.pc = reset_pc;
  ram_size = size;
  memory = new uint32_t[ram_size];

  if (image && *image) {
    std::ifstream inst_data(image, std::ios::in);
    if (!inst_data.is_open()) {
      std::cout << "Error: Image " << image << " does not exist" << std::endl;
      exit(0);
    }

    inst_data.seekg(0, std::ios::end);
    std::streamsize img_size = inst_data.tellg();
    inst_data.seekg(0, std::ios::beg);

    assert(img_size / 4 < ram_size);
    if (!inst_data.read(reinterpret_cast<char *>(memory + 0x80000000 / 4),
                        img_size)) {
      std::cerr << "读取文件失败！" << std::endl;
      exit(1);
    }
    inst_data.close();
  }

  memory[0x10000004 / 4] = 0x00006000; // 和进入 OpenSBI 相关
  memory[uint32_t(0x0 / 4)] = 0xf1402573;
  memory[uint32_t(0x4 / 4)] = 0x83e005b7;
  memory[uint32_t(0x8 / 4)] = 0x800002b7;
  memory[uint32_t(0xc / 4)] = 0x00028067;

  for (int i = 0; i < 32; i++) {
    state.gpr[i] = 0;
  }
  for (int i = 0; i < 21; i++) {
    state.csr[i] = 0;
  }
  state.csr[csr_misa] = 0x40141101;
  privilege = 0b11;

  state.store = false;
  asy = false;
  page_fault_inst = false;
  page_fault_load = false;
  page_fault_store = false;
  sim_time = 0;
}

void Ref_cpu::exec(const SimConfig &config) {
  uint64_t restored_inst_count = 0;

  // --- Main Loop ---
  while (!sim_end && sim_time < MAX_SIM_TIME) {
    if (sim_time % 100000000 == 0) {
      std::cout << "SimTime: " << sim_time << std::endl;
    }

    // Process one instruction
    step();

    // Check instruction limit for restore mode
    if (config.mode == SimMode::RESTORE && config.max_insts > 0) {
      restored_inst_count++;
      if (restored_inst_count >= config.max_insts) {
        std::cout << "Restore run finished (max_insts reached)." << std::endl;
        break;
      }
    }
  }
}

TraceInst Ref_cpu::step() {
  // Reset trace info
  current_trace = {};
  current_trace.is_wfi = false;
  current_trace.is_ebreak = false;
  
  // Execute one instruction
  RISCV();
  sim_time++;

  // Capture basic info (PC is updated in RISCV, so we might need the PC *before* execution or track it inside)
  // Actually, RISCV() updates state.pc to next_pc at the end. 
  // We should capture the PC of the instruction *just executed*.
  // However, RISCV() doesn't easily return the old PC unless we track it.
  // The structure of RISCV() in exec.cpp:
  //   uint32_t p_addr = state.pc;
  //   Instruction = memory[p_addr >> 2];
  //   ... decoding ...
  //   execution ...
  //   state.pc = next_pc;
  
  // So 'current_trace' needs to be populated *inside* RISCV() or its helpers 
  // while the information is available.
  
  return current_trace;
}

void Ref_cpu::exception(uint32_t trap_val) {
  is_exception = true;
  uint32_t next_pc = state.pc + 4;

  // 重新获取当前状态（因为exec可能没传进来最新的）
  bool ecall = (Instruction == INST_ECALL);
  bool mret = (Instruction == INST_MRET);
  bool sret = (Instruction == INST_SRET);

  uint32_t mstatus = state.csr[csr_mstatus];
  uint32_t sstatus = state.csr[csr_sstatus];
  uint32_t medeleg = state.csr[csr_medeleg];
  uint32_t mtvec = state.csr[csr_mtvec];
  uint32_t stvec = state.csr[csr_stvec];

  // 再次计算 Trap 原因 (与 RISCV() 中逻辑一致，但这里是为了确定是用 MTrap 还是
  // STrap 处理)
  // 注意：为了代码复用，这里其实可以简化，但为了保持你原有逻辑结构：

  bool medeleg_U_ecall = (medeleg >> 8) & 1;
  bool medeleg_S_ecall = (medeleg >> 9) & 1;
  bool medeleg_page_fault_inst = (medeleg >> 12) & 1;
  bool medeleg_page_fault_load = (medeleg >> 13) & 1;
  bool medeleg_page_fault_store = (medeleg >> 15) & 1;

  // 这里直接复用成员变量里的中断状态 (假设RISCV函数刚跑完，状态是新的)
  // 如果不是，需要重新计算 M_software_interrupt 等

  bool MTrap =
      (M_software_interrupt) || (M_timer_interrupt) || (M_external_interrupt) ||
      ((privilege == 0) && !medeleg_U_ecall && ecall) ||
      (ecall && (privilege == 1) && !medeleg_S_ecall) ||
      (ecall && (privilege == 3)) ||
      (page_fault_inst && !medeleg_page_fault_inst) ||
      (page_fault_load && !medeleg_page_fault_load) ||
      (page_fault_store && !medeleg_page_fault_store) || illegal_exception;

  bool STrap = S_software_interrupt || S_timer_interrupt ||
               S_external_interrupt ||
               (ecall && (privilege == 0) && medeleg_U_ecall) ||
               (ecall && (privilege == 1) && medeleg_S_ecall) ||
               (page_fault_inst && medeleg_page_fault_inst) ||
               (page_fault_load && medeleg_page_fault_load) ||
               (page_fault_store && medeleg_page_fault_store);

  if (MTrap) {
    state.csr[csr_mepc] = state.pc;
    uint32_t cause = 0;

    // 计算 MCause
    bool is_interrupt =
        M_software_interrupt || M_timer_interrupt || M_external_interrupt;
    if (is_interrupt)
      cause |= (1u << 31);

    uint32_t exception_code = 0;
    if (M_software_interrupt)
      exception_code = 3;
    else if (M_timer_interrupt)
      exception_code = 7;
    else if (M_external_interrupt ||
             (ecall && privilege == 3 && !medeleg_U_ecall))
      exception_code = 11;
    else if (ecall && privilege == 0 && !medeleg_U_ecall)
      exception_code = 8;
    else if (ecall && privilege == 1 && !medeleg_S_ecall)
      exception_code = 9;
    else if (page_fault_inst && !medeleg_page_fault_inst)
      exception_code = 12;
    else if (page_fault_load && !medeleg_page_fault_load)
      exception_code = 13;
    else if (page_fault_store && !medeleg_page_fault_store)
      exception_code = 15;
    else if (illegal_exception)
      exception_code = 2;

    cause |= exception_code;
    state.csr[csr_mcause] = cause;

    // 向量中断跳转
    if ((mtvec & 1) && (cause & (1u << 31))) {
      next_pc = (mtvec & 0xfffffffc) + 4 * (cause & 0x7fffffff);
    } else {
      next_pc =
          mtvec & 0xfffffffc; // 这里的MASK可能需要根据Spec确认，通常是清除低2位
    }

    // 更新 mstatus
    // MPP = privilege
    mstatus = (mstatus & ~MSTATUS_MPP) | ((privilege & 0x3) << 11);
    // MPIE = MIE
    if (mstatus & MSTATUS_MIE)
      mstatus |= MSTATUS_MPIE;
    else
      mstatus &= ~MSTATUS_MPIE;
    // MIE = 0
    mstatus &= ~MSTATUS_MIE;

    // 同步 sstatus (sstatus 是 mstatus 的影子)
    state.csr[csr_mstatus] = mstatus;
    state.csr[csr_sstatus] =
        mstatus & 0x800DE762; // 这是一个Mask，简单起见可以直接赋值

    privilege = 3; // Machine Mode
    state.csr[csr_mtval] = trap_val;

  } else if (STrap) {
    state.csr[csr_sepc] = state.pc;
    uint32_t cause = 0;

    bool is_interrupt =
        S_software_interrupt || S_timer_interrupt || S_external_interrupt;
    if (is_interrupt)
      cause |= (1u << 31);

    uint32_t exception_code = 0;
    if (S_external_interrupt || (ecall && privilege == 1 && medeleg_S_ecall))
      exception_code = 9;
    else if (S_timer_interrupt)
      exception_code = 5;
    else if (ecall && privilege == 0 && medeleg_U_ecall)
      exception_code = 8;
    else if (S_software_interrupt)
      exception_code = 1;
    else if (page_fault_inst && medeleg_page_fault_inst)
      exception_code = 12;
    else if (page_fault_load && medeleg_page_fault_load)
      exception_code = 13;
    else if (page_fault_store && medeleg_page_fault_store)
      exception_code = 15;

    cause |= exception_code;
    state.csr[csr_scause] = cause;

    if ((stvec & 1) && (cause & (1u << 31))) {
      next_pc = (stvec & 0xfffffffc) + 4 * (cause & 0x7fffffff);
    } else {
      next_pc = stvec & 0xfffffffc;
    }

    // 更新 sstatus
    // SPP = privilege
    if (privilege == 1)
      sstatus |= MSTATUS_SPP;
    else
      sstatus &= ~MSTATUS_SPP;

    // SPIE = SIE
    if (sstatus & MSTATUS_SIE)
      sstatus |= MSTATUS_SPIE;
    else
      sstatus &= ~MSTATUS_SPIE;

    // SIE = 0
    sstatus &= ~MSTATUS_SIE;

    // 写回
    state.csr[csr_sstatus] = sstatus;
    // 更新 mstatus 中对应的位 (SPIE, SIE, SPP 在 mstatus 中也有对应位置)
    // 简单做法：读出改完的 sstatus，把对应位刷回 mstatus
    uint32_t mask = MSTATUS_SIE | MSTATUS_SPIE | MSTATUS_SPP;
    state.csr[csr_mstatus] =
        (state.csr[csr_mstatus] & ~mask) | (sstatus & mask);

    privilege = 1; // Supervisor Mode
    state.csr[csr_stval] = trap_val;

  } else if (mret) {
    // MIE = MPIE
    if (mstatus & MSTATUS_MPIE)
      mstatus |= MSTATUS_MIE;
    else
      mstatus &= ~MSTATUS_MIE;

    // Privilege = MPP
    privilege = GET_MPP(mstatus);

    // MPIE = 1
    mstatus |= MSTATUS_MPIE;
    // MPP = U (0)
    mstatus &= ~MSTATUS_MPP;

    state.csr[csr_mstatus] = mstatus;
    // 同步 sstatus
    state.csr[csr_sstatus] =
        mstatus & 0x800DE762; // 仅示意，实际上 RISC-V 硬件会自动映射

    next_pc = state.csr[csr_mepc];

  } else if (sret) {
    // SIE = SPIE
    if (sstatus & MSTATUS_SPIE)
      sstatus |= MSTATUS_SIE;
    else
      sstatus &= ~MSTATUS_SIE;

    // Privilege = SPP
    privilege = GET_SPP(sstatus);

    // SPIE = 1
    sstatus |= MSTATUS_SPIE;
    // SPP = U (0)
    sstatus &= ~MSTATUS_SPP;

    state.csr[csr_sstatus] = sstatus;
    // 同步回 mstatus
    uint32_t mask = MSTATUS_SIE | MSTATUS_SPIE | MSTATUS_SPP;
    state.csr[csr_mstatus] =
        (state.csr[csr_mstatus] & ~mask) | (sstatus & mask);

    next_pc = state.csr[csr_sepc];
  }

  state.pc = next_pc;
}

void Ref_cpu::RISCV() {
  if (privilege == RISCV_MODE_U) {
    // BBV logic removed
  }

  is_csr = is_exception = is_br = br_taken = false;
  illegal_exception = page_fault_load = page_fault_inst = page_fault_store =
      asy = false;
  state.store = false;


  uint32_t p_addr = state.pc;
  
  // Capture Trace basic info
  current_trace.pc = state.pc;
  current_trace.is_branch = false;
  current_trace.is_load = false;
  current_trace.is_store = false;


  if ((state.csr[csr_satp] & 0x80000000) && privilege != 3) {
    page_fault_inst = !va2pa(p_addr, state.pc, 0);

    if (page_fault_inst) {
      exception(state.pc);
      return;
    } else {
      Instruction = memory[p_addr >> 2];
    }
  } else {
    Instruction = memory[p_addr >> 2];
  }
  
  current_trace.opcode = BITS(Instruction, 6, 0);
  current_trace.rd = BITS(Instruction, 11, 7);
  // RS1/RS2 will be set in specific helpers or we can just extract them always since it's cheap
  current_trace.rs1 = BITS(Instruction, 19, 15);
  current_trace.rs2 = BITS(Instruction, 24, 20);

  if (Instruction == INST_EBREAK) {
    state.pc += 4;
    std::cout << "sim_time: " << sim_time << std::endl;
    sim_end = true;
    return;
  }

  // === 优化 1: 极速解码 ===
  // 使用 BITS 宏直接提取字段，完全替代 bool 数组操作
  uint32_t opcode = BITS(Instruction, 6, 0);

  bool ecall = (Instruction == INST_ECALL);
  bool mret = (Instruction == INST_MRET);
  bool sret = (Instruction == INST_SRET);

  // === 优化 2: 快速读取 CSR 状态 ===
  uint32_t mstatus = state.csr[csr_mstatus];
  uint32_t mie_reg = state.csr[csr_mie];
  uint32_t mip_reg = state.csr[csr_mip];
  uint32_t mideleg = state.csr[csr_mideleg];
  uint32_t medeleg = state.csr[csr_medeleg];

  // 提取关键位
  bool mstatus_mie = (mstatus & MSTATUS_MIE) != 0;
  bool mstatus_sie = (mstatus & MSTATUS_SIE) != 0;

  // 异常委托位 (Exceptions)
  bool medeleg_U_ecall = (medeleg >> 8) & 1;
  bool medeleg_S_ecall = (medeleg >> 9) & 1;
  // bool medeleg_M_ecall = (medeleg >> 11) & 1; // 通常M-ecall不委托

  bool medeleg_page_fault_inst = (medeleg >> 12) & 1;
  bool medeleg_page_fault_load = (medeleg >> 13) & 1;
  bool medeleg_page_fault_store = (medeleg >> 15) & 1;

  // === 优化 3: 中断判断逻辑 (位运算) ===
  // M-mode 中断条件:Pending & Enabled & NotDelegated & (CurrentPriv < M ||
  // MIE=1)

  // Software Interrupts
  M_software_interrupt = (mip_reg & MIP_MSIP) && (mie_reg & MIP_MSIP) &&
                         !(mideleg & MIP_MSIP) &&
                         (privilege < 3 || mstatus_mie);

  // Timer Interrupts
  M_timer_interrupt = (mip_reg & MIP_MTIP) && (mie_reg & MIP_MTIP) &&
                      !(mideleg & MIP_MTIP) && (privilege < 3 || mstatus_mie);

  // External Interrupts
  M_external_interrupt = (mip_reg & MIP_MEIP) && (mie_reg & MIP_MEIP) &&
                         !(mideleg & MIP_MEIP) &&
                         (privilege < 3 || mstatus_mie);

  // S-mode 中断条件: Pending & Enabled & Delegated & (CurrentPriv < S || SIE=1)
  // 注意：privilege < 2 (S-mode=1, U-mode=0) 意味着当前是 U 或 S
  bool s_irq_enable = (privilege < 1 || (privilege == 1 && mstatus_sie));

  S_software_interrupt =
      (((mip_reg & MIP_MSIP) && (mie_reg & MIP_MSIP) && (mideleg & MIP_MSIP)) ||
       ((mip_reg & MIP_SSIP) && (mie_reg & MIP_SSIP))) &&
      (privilege < 2 && s_irq_enable);

  S_timer_interrupt =
      (((mip_reg & MIP_MTIP) && (mie_reg & MIP_MTIP) && (mideleg & MIP_MTIP)) ||
       ((mip_reg & MIP_STIP) && (mie_reg & MIP_STIP))) &&
      (privilege < 2 && s_irq_enable);

  S_external_interrupt =
      (((mip_reg & MIP_MEIP) && (mie_reg & MIP_MEIP) && (mideleg & MIP_MEIP)) ||
       ((mip_reg & MIP_SEIP) && (mie_reg & MIP_SEIP))) &&
      (privilege < 2 && s_irq_enable);

  // Trap 判断
  bool MTrap =
      M_software_interrupt || M_timer_interrupt || M_external_interrupt ||
      ((privilege == 0) && !medeleg_U_ecall && ecall) || // ecall from U
      ((privilege == 1) && !medeleg_S_ecall && ecall) || // ecall from S
      ((privilege == 3) && ecall) ||                     // ecall from M
      (page_fault_inst && !medeleg_page_fault_inst) || illegal_exception;

  bool STrap = S_software_interrupt || S_timer_interrupt ||
               S_external_interrupt ||
               ((privilege == 0) && medeleg_U_ecall && ecall) ||
               ((privilege == 1) && medeleg_S_ecall && ecall) ||
               (page_fault_inst && medeleg_page_fault_inst);

  asy = MTrap || STrap || mret || sret;

  // WFI/EBREAK 检查 (简单处理)
  if (Instruction == INST_EBREAK) {
    current_trace.is_ebreak = true;
    sim_end = true;
  }
  if (Instruction == INST_WFI && !asy && !page_fault_inst && !page_fault_load &&
      !page_fault_store) {
    current_trace.is_wfi = true;
    sim_end = true;
  }

  if (page_fault_inst) {
    exception(state.pc);
  } else if (illegal_exception) {
    exception(Instruction);
  } else if (asy || Instruction == INST_ECALL) {
    exception(0);
  } else if (opcode == number_10_opcode_ecall) {
    // SYSTEM 指令 (CSR, WFI, MRET等)
    if (Instruction == INST_WFI) {
      is_csr = false;
    } else {
      is_csr = true;
    }
    RV32CSR();
  } else if (opcode == number_11_opcode_lrw) {
    RV32A();
  } else {
    RV32IM();
  }
  state.gpr[0] = 0;
}

void Ref_cpu::RV32CSR() {
  // pc + 4
  uint32_t next_pc = state.pc + 4;

  // 使用宏直接提取，无需 copy_indice
  uint32_t rd = BITS(Instruction, 11, 7);
  uint32_t rs1 = BITS(Instruction, 19, 15);
  uint32_t uimm = rs1; // 对于立即数CSR指令，rs1字段就是立即数
  uint32_t csr_addr = BITS(Instruction, 31, 20);
  uint32_t funct3 = BITS(Instruction, 14, 12);

  uint32_t reg_rdata1 = state.gpr[rs1];

  bool we = funct3 == 1 || rs1 != 0;
  bool re = funct3 != 1 || rd != 0;
  uint32_t wcmd = funct3 & 0b11;
  uint32_t csr_wdata, wdata;

  if (funct3 & 0b100) {
    wdata = rs1;
  } else {
    wdata = reg_rdata1;
  }

  if (csr_addr != number_mtvec && csr_addr != number_mepc &&
      csr_addr != number_mcause && csr_addr != number_mie &&
      csr_addr != number_mip && csr_addr != number_mtval &&
      csr_addr != number_mscratch && csr_addr != number_mstatus &&
      csr_addr != number_mideleg && csr_addr != number_medeleg &&
      csr_addr != number_sepc && csr_addr != number_stvec &&
      csr_addr != number_scause && csr_addr != number_sscratch &&
      csr_addr != number_stval && csr_addr != number_sstatus &&
      csr_addr != number_sie && csr_addr != number_sip &&
      csr_addr != number_satp && csr_addr != number_mhartid &&
      csr_addr != number_misa && csr_addr != number_time &&
      csr_addr != number_timeh) {
    ;
  } else if (csr_addr == number_time || csr_addr == number_timeh) {
    illegal_exception = true;
    exception(Instruction);
    return;
  } else {

    int csr_idx = cvt_number_to_csr(csr_addr);
    if (re) {
      state.gpr[rd] = state.csr[csr_idx];
    }

    if (we) {
      uint32_t csr_wdata;
      if (wcmd == CSR_W) {
        csr_wdata = wdata;
      } else if (wcmd == CSR_S) {
        csr_wdata = wdata | (~wdata & state.csr[csr_idx]);
      } else if (wcmd == CSR_C) {
        csr_wdata = (~wdata & state.csr[csr_idx]);
      }

      if (csr_idx == csr_mie || csr_idx == csr_sie) {
        if (csr_idx == csr_sie)
          csr_wdata =
              (state.csr[csr_mie] & 0xfffffccc) | (csr_wdata & 0x00000333);
        else
          csr_wdata =
              (state.csr[csr_mie] & 0xfffff444) | (csr_wdata & 0x00000bbb);

        state.csr[csr_mie] = csr_wdata;
        state.csr[csr_sie] = csr_wdata;
      } else if (csr_idx == number_mip || csr_idx == number_sip) {

        if (csr_idx == number_mip)
          csr_wdata =
              (state.csr[csr_mip] & 0xfffffccc) | (csr_wdata & 0x00000333);
        else
          csr_wdata =
              (state.csr[csr_mip] & 0xfffff444) | (csr_wdata & 0x00000bbb);

        state.csr[csr_mip] = csr_wdata;
        state.csr[csr_sip] = csr_wdata;
      } else if (csr_idx == csr_mstatus || csr_idx == csr_sstatus) {

        if (csr_idx == csr_sstatus) {
          csr_wdata = (state.csr[csr_sstatus] & 0x7ff21ecc) |
                      (csr_wdata & (~0x7ff21ecc));
        } else {
          csr_wdata = (state.csr[csr_mstatus] & 0x7f800644) |
                      (csr_wdata & (~0x7f800644));
        }

        state.csr[csr_mstatus] = csr_wdata;
        state.csr[csr_sstatus] = csr_wdata;

      } else {
        state.csr[csr_idx] = csr_wdata;
      }
    }
  }

  state.pc = next_pc;
}

void Ref_cpu::RV32A() {
  // pc + 4
  uint32_t next_pc = state.pc + 4;
  uint32_t funct5 = BITS(Instruction, 31, 27);
  uint32_t reg_d_index = BITS(Instruction, 11, 7);
  uint32_t reg_a_index = BITS(Instruction, 19, 15);
  uint32_t reg_b_index = BITS(Instruction, 24, 20);

  uint32_t reg_rdata1 = state.gpr[reg_a_index];
  uint32_t reg_rdata2 = state.gpr[reg_b_index];

  uint32_t v_addr = reg_rdata1;
  uint32_t p_addr = v_addr;

  if ((state.csr[csr_satp] & 0x80000000) && privilege != 3) {
    bool page_fault_1 = !va2pa(p_addr, v_addr, 1);
    bool page_fault_2 = !va2pa(p_addr, v_addr, 2);

    if (page_fault_1 || page_fault_2) {
      if (funct5 == 2) {
        if (page_fault_1) {
          page_fault_load = true;
        }
      } else if (funct5 == 3) {
        if (page_fault_2) {
          page_fault_store = true;
        }
      } else {
        page_fault_store = true;
      }
    }

    if (page_fault_load || page_fault_store) {
      exception(v_addr);
      return;
    }
  }

  if (funct5 != 2) {
    state.store = true;
    state.store_addr = p_addr;
    state.store_strb = 0b1111;
  }

  switch (funct5) {
  case 0: { // amoadd.w
    state.gpr[reg_d_index] = memory[p_addr >> 2];
    state.store_data = memory[p_addr >> 2] + reg_rdata2;
    break;
  }
  case 1: { // amoswap.w
    state.gpr[reg_d_index] = memory[p_addr >> 2];
    state.store_data = reg_rdata2;
    break;
  }
  case 2: { // lr.w
    state.gpr[reg_d_index] = memory[p_addr >> 2];
    break;
  }
  case 3: { // sc.w
    state.store_data = reg_rdata2;
    state.gpr[reg_d_index] = 0;
    break;
  }
  case 4: { // amoxor.w
    state.gpr[reg_d_index] = memory[p_addr >> 2];
    state.store_data = memory[p_addr >> 2] ^ reg_rdata2;
    break;
  }
  case 8: { // amoor.w
    state.gpr[reg_d_index] = memory[p_addr >> 2];
    state.store_data = memory[p_addr >> 2] | reg_rdata2;
    break;
  }
  case 12: { // amoand.w
    state.gpr[reg_d_index] = memory[p_addr >> 2];
    state.store_data = memory[p_addr >> 2] & reg_rdata2;
    break;
  }
  case 16: { // amomin.w
    state.gpr[reg_d_index] = memory[p_addr >> 2];
    state.store_data = ((int32_t)memory[p_addr >> 2] > (int32_t)reg_rdata2)
                           ? reg_rdata2
                           : memory[p_addr >> 2];
    break;
  }
  case 20: { // amomax.w
    state.gpr[reg_d_index] = memory[p_addr >> 2];
    state.store_data = ((int32_t)memory[p_addr >> 2] > (int32_t)reg_rdata2)
                           ? memory[p_addr >> 2]
                           : reg_rdata2;
    break;
  }
  case 24: { // amominu.w
    state.gpr[reg_d_index] = memory[p_addr >> 2];
    state.store_data = ((uint32_t)memory[p_addr >> 2] < (uint32_t)reg_rdata2)
                           ? memory[p_addr >> 2]
                           : reg_rdata2;
    break;
  }
  case 28: { // amomaxu.w
    state.gpr[reg_d_index] = memory[p_addr >> 2];
    state.store_data = ((uint32_t)memory[p_addr >> 2] > (uint32_t)reg_rdata2)
                           ? memory[p_addr >> 2]
                           : reg_rdata2;
    break;
  }
  default: {
    break;
  }
  }

  // Atomic operations generally involve a load and a store.
  // We can simplify and mark them as Store for dependency purposes, 
  // or Load+Store if we supported it. 
  // Let's mark as Store to be safe for memory dependencies.
  if (funct5 != 2) { // Not LR.W
    current_trace.is_store = true;
    current_trace.mem_addr = p_addr;
  }
  // LR.W is a load
  if (funct5 == 2) {
    current_trace.is_load = true;
    current_trace.mem_addr = p_addr;
  } else if (funct5 == 3) { // SC.W
      // SC is conditional store
      current_trace.is_store = true;
      current_trace.mem_addr = p_addr;
  } else {
      // AMOs are Read-Modify-Write. Mark as Store (which implies a Write)
      // For dependency tracking in a simple simulator, Store is usually the barrier.
      current_trace.is_load = true; // AMOs also read
      current_trace.is_store = true;
      current_trace.mem_addr = p_addr;
  }



  store_data();
  state.pc = next_pc;
}

void Ref_cpu::RV32IM() {
  // pc + 4
  uint32_t next_pc = state.pc + 4;
  uint32_t opcode = BITS(Instruction, 6, 0);
  uint32_t funct3 = BITS(Instruction, 14, 12);
  uint32_t funct7 = BITS(Instruction, 31, 25);
  uint32_t reg_d_index = BITS(Instruction, 11, 7);
  uint32_t reg_a_index = BITS(Instruction, 19, 15);
  uint32_t reg_b_index = BITS(Instruction, 24, 20);

  uint32_t reg_rdata1 = state.gpr[reg_a_index];
  uint32_t reg_rdata2 = state.gpr[reg_b_index];

  switch (opcode) {
  case number_0_opcode_lui: { // lui
    state.gpr[reg_d_index] = immU(Instruction);
    break;
  }
  case number_1_opcode_auipc: { // auipc
    bool bit_temp[32];
    state.gpr[reg_d_index] = immU(Instruction) + state.pc;
    break;
  }
  case number_2_opcode_jal: { // jal
    is_br = true;
    br_taken = true;
    
    current_trace.is_branch = true;
    current_trace.branch_taken = true;
    current_trace.target = state.pc + immJ(Instruction);
    
    next_pc = state.pc + immJ(Instruction);
    state.gpr[reg_d_index] = state.pc + 4;
    break;
  }
  case number_3_opcode_jalr: { // jalr
    is_br = true;
    br_taken = true;
    bool bit_temp[32];
    next_pc = (reg_rdata1 + immI(Instruction)) & 0xFFFFFFFC;
    
    current_trace.is_branch = true;
    current_trace.branch_taken = true;
    current_trace.target = next_pc;
    
    state.gpr[reg_d_index] = state.pc + 4;
    break;
  }
  case number_4_opcode_beq: { // beq, bne, blt, bge, bltu, bgeu
    is_br = true;
    current_trace.is_branch = true;
    current_trace.target = state.pc + immB(Instruction);
    
    switch (funct3) {
    case 0: { // beq
      if (reg_rdata1 == reg_rdata2) {
        br_taken = true;
        current_trace.branch_taken = true;
        next_pc = (state.pc + immB(Instruction));
      }
      break;
    }
    case 1: { // bne
      if (reg_rdata1 != reg_rdata2) {
        br_taken = true;
        current_trace.branch_taken = true;
        next_pc = (state.pc + immB(Instruction));
      }
      break;
    }
    case 4: { // blt
      if ((int32_t)reg_rdata1 < (int32_t)reg_rdata2) {
        br_taken = true;
        current_trace.branch_taken = true;
        next_pc = (state.pc + immB(Instruction));
      }
      break;
    }
    case 5: { // bge
      if ((int32_t)reg_rdata1 >= (int32_t)reg_rdata2) {
        br_taken = true;
        current_trace.branch_taken = true;
        next_pc = (state.pc + immB(Instruction));
      }
      break;
    }
    case 6: { // bltu
      if ((uint32_t)reg_rdata1 < (uint32_t)reg_rdata2) {
        br_taken = true;
        current_trace.branch_taken = true;
        next_pc = (state.pc + immB(Instruction));
      }
      break;
    }
    case 7: { // bgeu
      if ((uint32_t)reg_rdata1 >= (uint32_t)reg_rdata2) {
        br_taken = true;
        current_trace.branch_taken = true;
        next_pc = (state.pc + immB(Instruction));
      }
      break;
    }
    }
    break;
  }
  case number_5_opcode_lb: { // lb, lh, lw, lbu, lhu
    uint32_t v_addr = reg_rdata1 + immI(Instruction);
    uint32_t p_addr = v_addr;
    
    current_trace.is_load = true;
    current_trace.mem_addr = v_addr;
    
    if ((state.csr[csr_satp] & 0x80000000) && privilege != 3) {
      page_fault_load = !va2pa(p_addr, v_addr, 1);
    }

    if (page_fault_load) {
      exception(v_addr);
      return;

    } else {
      uint32_t data = memory[p_addr >> 2];
      uint32_t offset = p_addr & 0b11;
      uint32_t size = funct3 & 0b11;
      uint32_t sign = 0, mask;
      data = data >> (offset * 8);
      if (size == 0) {
        mask = 0xFF;
        if (data & 0x80)
          sign = 0xFFFFFF00;
      } else if (size == 0b01) {
        mask = 0xFFFF;
        if (data & 0x8000)
          sign = 0xFFFF0000;
      } else {
        mask = 0xFFFFFFFF;
      }

      data = data & mask;

      // 有符号数
      if (!(funct3 & 0b100)) {
        data = data | sign;
      }

      if (p_addr == 0x1fd0e000) {
        data = sim_time;
      }
      if (p_addr == 0x1fd0e004) {
        data = 0;
      }

      state.gpr[reg_d_index] = data;
    }
    break;
  }
  case number_6_opcode_sb: { // sb, sh, sw

    uint32_t v_addr = reg_rdata1 + immS(Instruction);
    uint32_t p_addr = v_addr;
    
    current_trace.is_store = true;
    current_trace.mem_addr = v_addr;
    
    if ((state.csr[csr_satp] & 0x80000000) && privilege != 3) {
      page_fault_store = !va2pa(p_addr, v_addr, 2);
    }

    if (page_fault_store) {
      exception(v_addr);
      return;
    } else {

      state.store = true;
      state.store_addr = p_addr;
      state.store_data = reg_rdata2;
      if (funct3 == 0b00) {
        state.store_strb = 0b1;
        state.store_data &= 0xFF;
      } else if (funct3 == 0b01) {
        state.store_strb = 0b11;
        state.store_data &= 0xFFFF;
      } else {
        state.store_strb = 0b1111;
      }

      store_data();
    }

    break;
  }
  case number_7_opcode_addi: { // addi, slti, sltiu, xori, ori, andi, slli,
                               // srli, srai
    switch (funct3) {
    case 0: { // addi
      state.gpr[reg_d_index] = reg_rdata1 + immI(Instruction);
      break;
    }
    case 2: { // slti
      state.gpr[reg_d_index] =
          (int32_t)reg_rdata1 < (int32_t)immI(Instruction) ? 1 : 0;
      break;
    }
    case 3: { // sltiu
      state.gpr[reg_d_index] =
          (uint32_t)reg_rdata1 < (uint32_t)immI(Instruction) ? 1 : 0;
      break;
    }
    case 4: { // xori
      state.gpr[reg_d_index] = reg_rdata1 ^ immI(Instruction);
      break;
    }
    case 6: { // ori
      state.gpr[reg_d_index] = reg_rdata1 | immI(Instruction);
      break;
    }
    case 7: { // andi
      state.gpr[reg_d_index] = reg_rdata1 & immI(Instruction);
      break;
    }
    case 1: { // slli
      state.gpr[reg_d_index] = reg_rdata1 << immI(Instruction);
      break;
    }
    case 5: { // srli, srai
      switch (funct7) {
      case 0: { // srli
        state.gpr[reg_d_index] = (uint32_t)reg_rdata1 >> immI(Instruction);
        break;
      }
      case 32: { // srai
        state.gpr[reg_d_index] = (int32_t)reg_rdata1 >> immI(Instruction);
        break;
      }
      }
      break;
    }
    }
    break;
  }
  case number_8_opcode_add: { // add, sub, sll, slt, sltu, xor, srl, sra, or,
                              // and
    if (funct7 == 1) {        // mul div
      int64_t s1 = (int64_t)(int32_t)reg_rdata1;
      int64_t s2 = (int64_t)(int32_t)reg_rdata2;

      uint64_t u1 = (uint32_t)reg_rdata1;
      uint64_t u2 = (uint32_t)reg_rdata2;

      // 获取 32 位操作数
      int32_t dividend = (int32_t)reg_rdata1;
      int32_t divisor = (int32_t)reg_rdata2;
      uint32_t u_dividend = (uint32_t)reg_rdata1;
      uint32_t u_divisor = (uint32_t)reg_rdata2;

      switch (funct3) {
      case 0: { // mul
        state.gpr[reg_d_index] = (int32_t)(u1 * u2);
        break;
      }
      case 1: { // mulh
        state.gpr[reg_d_index] = (uint32_t)((s1 * s2) >> 32);
        break;
      }
      case 2: { // mulsu
        state.gpr[reg_d_index] = (uint32_t)((s1 * (int64_t)u2) >> 32);
        break;
      }
      case 3: { // mulhu
        state.gpr[reg_d_index] = (uint32_t)((u1 * u2) >> 32);
        break;
      }
      case 4: { // div (signed)
        if (divisor == 0) {
          state.gpr[reg_d_index] = -1; // RISC-V 规定：除以0结果为 -1
        } else if (dividend == INT32_MIN && divisor == -1) {
          state.gpr[reg_d_index] =
              INT32_MIN; // RISC-V 规定：溢出时结果为被除数本身(INT_MIN)
        } else {
          state.gpr[reg_d_index] = dividend / divisor;
        }
        break;
      }
      case 5: { // divu (unsigned)
        if (u_divisor == 0) {
          state.gpr[reg_d_index] = 0xFFFFFFFF; // RISC-V 规定：除以0结果为最大值
        } else {
          state.gpr[reg_d_index] = u_dividend / u_divisor;
        }
        break;
      }
      case 6: { // rem (signed)
        if (divisor == 0) {
          state.gpr[reg_d_index] = dividend; // RISC-V 规定：除以0，余数为被除数
        } else if (dividend == INT32_MIN && divisor == -1) {
          state.gpr[reg_d_index] = 0; // RISC-V 规定：溢出时，余数为 0
        } else {
          state.gpr[reg_d_index] = dividend % divisor;
        }
        break;
      }
      case 7: { // remu (unsigned)
        if (u_divisor == 0) {
          state.gpr[reg_d_index] =
              u_dividend; // RISC-V 规定：除以0，余数为被除数
        } else {
          state.gpr[reg_d_index] = u_dividend % u_divisor;
        }
        break;
      }
      }
    } else {
      switch (funct3) {
      case 0: { // add, sub
        switch (funct7) {
        case 0: { // add
          state.gpr[reg_d_index] = reg_rdata1 + reg_rdata2;
          break;
        }
        case 32: { // sub
          state.gpr[reg_d_index] = reg_rdata1 - reg_rdata2;
          break;
        }
        }
        break;
      }
      case 1: { // sll
        state.gpr[reg_d_index] = reg_rdata1 << reg_rdata2;
        break;
      }
      case 2: { // slt
        state.gpr[reg_d_index] =
            (int32_t)reg_rdata1 < (int32_t)reg_rdata2 ? 1 : 0;
        break;
      }
      case 3: { // sltu
        state.gpr[reg_d_index] =
            (uint32_t)reg_rdata1 < (uint32_t)reg_rdata2 ? 1 : 0;
        break;
      }
      case 4: { // xor
        state.gpr[reg_d_index] = reg_rdata1 ^ reg_rdata2;
        break;
      }
      case 5: { // srl, sra
        switch (funct7) {
        case 0: { // srl
          state.gpr[reg_d_index] = (uint32_t)reg_rdata1 >> reg_rdata2;
          break;
        }
        case 32: { // sra
          state.gpr[reg_d_index] = (int32_t)reg_rdata1 >> reg_rdata2;
          break;
        }
        }
        break;
      }
      case 6: { // or
        state.gpr[reg_d_index] = reg_rdata1 | reg_rdata2;
        break;
      }
      case 7: { // and
        state.gpr[reg_d_index] = reg_rdata1 & reg_rdata2;
        break;
      }
      }
    }
    break;
  }
  case number_9_opcode_fence: { // fence, fence.i
    break;
  }
  default: {
    break;
  }
  }

  state.pc = next_pc;
}

void Ref_cpu::store_data() {

  uint32_t p_addr = state.store_addr;
  int offset = p_addr & 0x3;
  uint32_t wstrb = state.store_strb << offset;
  uint32_t wdata = state.store_data << (offset * 8);
  uint32_t old_data = memory[p_addr / 4];
  uint32_t mask = 0;

  if (wstrb & 0b1)
    mask |= 0xFF;
  if (wstrb & 0b10)
    mask |= 0xFF00;
  if (wstrb & 0b100)
    mask |= 0xFF0000;
  if (wstrb & 0b1000)
    mask |= 0xFF000000;
  /*if ((number_funct3_unsigned == 1 && p_addr % 2 == 1) ||*/
  /*    (number_funct3_unsigned == 2 && p_addr % 4 != 0)) {*/
  /*  cout << "Store Memory Address Align Error!!!" << endl;*/
  /*  cout << "funct3 code: " << dec << number_funct3_unsigned << endl;*/
  /*  cout << "addr: " << hex << p_addr << endl;*/
  /*  exit(-1);*/
  /*}*/

  memory[p_addr / 4] = (mask & wdata) | (~mask & old_data);

  if (p_addr == UART_BASE) {
    char temp;
    temp = wdata & 0x000000ff;
    memory[0x10000000 / 4] = memory[0x10000000 / 4] & 0xffffff00;
    std::cout << temp;
  }

  if (p_addr == 0x10000001 && (state.store_data & 0x000000ff) == 7) {
    memory[0xc201004 / 4] = 0xa;
    memory[0x10000000 / 4] = memory[0x10000000 / 4] & 0xfff0ffff;

    state.csr[csr_mip] = state.csr[csr_mip] | (1 << 9);
    state.csr[csr_sip] = state.csr[csr_sip] | (1 << 9);
  }

  if (p_addr == 0x10000001 && (state.store_data & 0x000000ff) == 5) {
    memory[0x10000000 / 4] = memory[0x10000000 / 4] & 0xfff0ffff | 0x00030000;
  }

  if (p_addr == 0xc201004 && (state.store_data & 0x000000ff) == 0xa) {
    memory[0xc201004 / 4] = 0x0;
    state.csr[csr_mip] = state.csr[csr_mip] & ~(1 << 9);
    state.csr[csr_sip] = state.csr[csr_sip] & ~(1 << 9);
  }
}

bool Ref_cpu::va2pa(uint32_t &p_addr, uint32_t v_addr, uint32_t type) {
  uint32_t mstatus = state.csr[csr_mstatus];
  uint32_t sstatus = state.csr[csr_sstatus];
  uint32_t satp = state.csr[csr_satp];

  // 1. 提取状态位 (直接位运算，极快)
  bool mxr = (mstatus & MSTATUS_MXR) != 0;
  bool sum = (mstatus & MSTATUS_SUM) != 0;
  bool mprv = (mstatus & MSTATUS_MPRV) != 0;

  // 确定有效特权级 (Effective Privilege Mode)
  // 如果 MPRV=1 且不是取指(type!=0)，则使用 MPP 作为特权级进行检查
  int eff_priv = privilege;
  if (type != 0 && mprv) {
    eff_priv = (mstatus >> MSTATUS_MPP_SHIFT) & 0x3;
  }

  // 2. Level 1 Page Table Walk
  // satp 的 PPN 字段在 SV32 中是低 22 位 (0-21)
  // VPN[1] 是 v_addr 的 [31:22] 位
  // pte1_addr = (satp.ppn << 12) + (vpn1 * 4)
  // 你的原代码逻辑：(satp << 12) | ((v_addr >> 20) & 0xFFC)
  // 等价于下面的位操作：
  uint32_t ppn_root = satp & 0x3FFFFF; // 提取 SATP 中的 PPN
  uint32_t vpn1 = (v_addr >> 22) & 0x3FF;
  uint32_t pte1_addr = (ppn_root << 12) | (vpn1 << 2);

  // 直接读取，注意这里需要确保 memory 是按字寻址还是字节寻址
  uint32_t pte1 = memory[pte1_addr >> 2];

  // 3. 检查 PTE 有效性
  // !V 或者 (!R && W) 都是无效的
  if (!(pte1 & PTE_V) || (!(pte1 & PTE_R) && (pte1 & PTE_W))) {
    return false;
  }

  // 4. 判断是否是叶子节点 (R=1 或 X=1)
  if ((pte1 & PTE_R) || (pte1 & PTE_X)) {
    // --- Superpage (4MB) ---

    // 权限检查 (Permission Check)
    // Fetch (0): 需要 X
    if (type == 0 && !(pte1 & PTE_X))
      return false;
    // Load (1): 需要 R，或者 (MXR=1 且 X=1)
    if (type == 1 && !(pte1 & PTE_R) && !(mxr && (pte1 & PTE_X)))
      return false;
    // Store (2): 需要 W
    if (type == 2 && !(pte1 & PTE_W))
      return false;

    // 用户权限检查 (User/Supervisor Check)
    bool is_user_page = (pte1 & PTE_U) != 0;
    if (eff_priv == 0 && !is_user_page)
      return false; // U-mode 访问 S-page -> Fault
    if (eff_priv == 1 && is_user_page && !sum)
      return false; // S-mode 访问 U-page 且 SUM=0 -> Fault

    // 对齐检查 (Superpage 要求 PPN[0] 为 0)
    // PPN[0] 对应 PTE 的 [19:10] 位
    if ((pte1 >> 10) & 0x3FF)
      return false;

    // A/D 位检查
    if (!(pte1 & PTE_A))
      return false; // Accessed 必须为 1 (硬件不自动设置时需报错)
    if (type == 2 && !(pte1 & PTE_D))
      return false; // 写操作 Dirty 必须为 1

    // 计算物理地址 (Superpage)
    // PA = PPN[1] | VPN[0] | Offset
    // PPN[1] 是 PTE[31:20]，对应 PA[31:22]
    // v_addr & 0x3FFFFF 保留低 22 位 (VPN[0] + Offset)
    p_addr = ((pte1 << 2) & 0xFFC00000) | (v_addr & 0x3FFFFF);
    return true;
  }

  // 5. Level 2 Page Table Walk (非叶子节点，指向下一级页表)
  // PPN 是 PTE 的 [31:10] 位
  uint32_t ppn1 = (pte1 >> 10) & 0x3FFFFF;
  uint32_t vpn0 = (v_addr >> 12) & 0x3FF;
  uint32_t pte2_addr = (ppn1 << 12) | (vpn0 << 2);

  uint32_t pte2 = memory[pte2_addr >> 2];

  // 重复有效性检查
  if (!(pte2 & PTE_V) || (!(pte2 & PTE_R) && (pte2 & PTE_W))) {
    return false;
  }

  // Level 2 必须是叶子节点 (SV32 只有两级)
  if ((pte2 & PTE_R) || (pte2 & PTE_X)) {
    // --- 4KB Page ---

    // 权限检查 (逻辑同上)
    if (type == 0 && !(pte2 & PTE_X))
      return false;
    if (type == 1 && !(pte2 & PTE_R) && !(mxr && (pte2 & PTE_X)))
      return false;
    if (type == 2 && !(pte2 & PTE_W))
      return false;

    // 用户权限检查
    bool is_user_page = (pte2 & PTE_U) != 0;
    if (eff_priv == 0 && !is_user_page)
      return false;
    if (eff_priv == 1 && is_user_page && !sum)
      return false;

    // A/D 位检查
    if (!(pte2 & PTE_A))
      return false;
    if (type == 2 && !(pte2 & PTE_D))
      return false;

    // 计算物理地址 (4KB Page)
    // PA = PPN | Offset
    // PPN 是 PTE[31:10]，对应 PA[31:12]
    // Offset 是 v_addr[11:0]
    p_addr = ((pte2 >> 10) << 12) | (v_addr & 0xFFF);
    return true;
  }

  return false; // 如果 Level 2 还不是叶子节点，则是非法页表
}
