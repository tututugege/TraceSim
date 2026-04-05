#pragma once
#define PHYSICAL_MEMORY_LENGTH (1024 * 1024 * 1024)

#define UART_BASE 0x10000000
#define UART_MMIO_SIZE 0x00000100
#define PLIC_BASE 0x0c000000
#define PLIC_MMIO_SIZE 0x00210000
#define TIMER_BASE 0x1fd0e000
#define INST_EBREAK 0x00100073
#define INST_ECALL 0x00000073
#define INST_MRET 0x30200073
#define INST_SRET 0x10200073
#define INST_WFI 0x10500073
#define INST_NOP 0x00000013

enum enum_number_opcode {
  number_0_opcode_lui = 0b0110111,   // lui
  number_1_opcode_auipc = 0b0010111, // auipc
  number_2_opcode_jal = 0b1101111,   // jal
  number_3_opcode_jalr = 0b1100111,  // jalr
  number_4_opcode_beq = 0b1100011,   // beq, bne, blt, bge, bltu, bgeu
  number_5_opcode_lb = 0b0000011,    // lb, lh, lw, lbu, lhu
  number_6_opcode_sb = 0b0100011,    // sb, sh, sw
  number_7_opcode_addi =
      0b0010011, // addi, slti, sltiu, xori, ori, andi, slli, srli, srai
  number_8_opcode_add =
      0b0110011, // add, sub, sll, slt, sltu, xor, srl, sra, or, and
  number_9_opcode_fence = 0b0001111, // fence, fence.i
  number_10_opcode_ecall =
      0b1110011, // ecall, ebreak, csrrw, csrrs, csrrc, csrrwi, csrrsi, csrrci
  number_11_opcode_lrw =
      0b0101111, // lr.w, sc.w, amoswap.w, amoadd.w, amoxor.w, amoand.w,
                 // amoor.w, amomin.w, amomax.w, amominu.w, amomaxu.w
};
