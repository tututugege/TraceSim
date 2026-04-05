#pragma once
#include "RISCV.h"
#include <cstdint>
#include <fstream>
#include <unordered_map>
#include <vector>
#include "Trace.h"

#define FORCE_INLINE __attribute__((always_inline)) inline

#define RISCV_MODE_U 0b00
#define RISCV_MODE_S 0b01
#define RISCV_MODE_M 0b11

#define BITMASK(bits) ((1ull << (bits)) - 1)
#define BITS(x, hi, lo)                                                        \
  (((x) >> (lo)) & BITMASK((hi) - (lo) + 1)) // similar to x[hi:lo] in verilog
#define SEXT(x, len)                                                           \
  ({                                                                           \
    struct {                                                                   \
      int64_t n : len;                                                         \
    } __x = {.n = (int64_t)x};                                                 \
    (uint64_t) __x.n;                                                          \
  })

#define immI(i) SEXT(BITS(i, 31, 20), 12)
#define immU(i) (SEXT(BITS(i, 31, 12), 20) << 12)
#define immS(i) ((SEXT(BITS(i, 31, 25), 7) << 5) | BITS(i, 11, 7))
#define immJ(i)                                                                \
  ((SEXT(BITS(i, 31, 31), 1) << 20) | (BITS(i, 19, 12) << 12) |                \
   (BITS(i, 20, 20) << 11) | (BITS(i, 30, 21) << 1))
#define immB(i)                                                                \
  ((SEXT(BITS(i, 31, 31), 1) << 12) | (BITS(i, 7, 7) << 11) |                  \
   (BITS(i, 30, 25) << 5) | (BITS(i, 11, 8) << 1))

// ================= CSR Bit Masks (Standard RISC-V) =================
#define MSTATUS_MIE (1 << 3)
#define MSTATUS_MPIE (1 << 7)
#define MSTATUS_SIE (1 << 1)
#define MSTATUS_SPIE (1 << 5)
#define MSTATUS_MPP (3 << 11) // Bits 11-12
#define MSTATUS_SPP (1 << 8)  // Bit 8

#define MIP_SSIP (1 << 1)
#define MIP_MSIP (1 << 3)
#define MIP_STIP (1 << 5)
#define MIP_MTIP (1 << 7)
#define MIP_SEIP (1 << 9)
#define MIP_MEIP (1 << 11)

// 获取 MPP 的值 (0=U, 1=S, 3=M)
#define GET_MPP(x) ((x >> 11) & 0x3)
// 获取 SPP 的值
#define GET_SPP(x) ((x >> 8) & 0x1)

// SV32 Page Table Entry (PTE) Bits
#define PTE_V (1 << 0) // Valid
#define PTE_R (1 << 1) // Read
#define PTE_W (1 << 2) // Write
#define PTE_X (1 << 3) // Execute
#define PTE_U (1 << 4) // User
#define PTE_G (1 << 5) // Global
#define PTE_A (1 << 6) // Accessed
#define PTE_D (1 << 7) // Dirty

// MSTATUS bits needed for translation
#define MSTATUS_MXR (1 << 19)
#define MSTATUS_SUM (1 << 18)
#define MSTATUS_MPRV (1 << 17)
#define MSTATUS_MPP_SHIFT 11

// ===================================================================
//
#define MAX_SIM_TIME 500000000000

// --- 1. 定义四种运行模式 ---
enum class SimMode {
  NORMAL,         // 0. Process instructions until completion
  GEN_BBV,        // 1. Generate BBV (compat with simpoint_sim exec path)
  GEN_CHECKPOINT, // 2. Generate checkpoints from SimPoint points
  RESTORE,        // 3. CKPT mode: restore from checkpoint and run
  TRACE           // 4. Trace-based simulation
};

// --- 2. 配置结构体 ---
struct SimConfig {
  SimMode mode = SimMode::NORMAL;
  std::string image_file;
  uint32_t ram_size = PHYSICAL_MEMORY_LENGTH;
  bool difftest = false;

  // Mode: GEN_BBV
  std::string bbv_output_file;

  // Mode: GEN_CHECKPOINT
  std::string points_file;
  std::string weights_file;
  std::string checkpoint_dir;

  // Mode: CKPT (--mode ckpt)
  std::string restore_file; // Checkpoint file path
  uint64_t max_insts = 0;   // Max instructions to run (0 = infinite)
};

typedef struct CPU_state {
  uint32_t gpr[32];
  uint32_t csr[21];
  uint32_t pc;

  uint32_t store_addr;
  uint32_t store_data;
  uint32_t store_strb;
  bool store;
  bool reserve_valid;
  uint32_t reserve_addr;
} CPU_state;

class Ref_cpu {
public:
  ~Ref_cpu();
  uint32_t *memory;
  std::unordered_map<uint32_t, uint32_t> io_words;
  uint32_t ram_size;
  uint32_t Instruction;
  CPU_state state;
  uint8_t privilege;
  bool asy;
  bool page_fault_inst;
  bool page_fault_load;
  bool page_fault_store;
  bool illegal_exception;

  bool M_software_interrupt;
  bool M_timer_interrupt;
  bool M_external_interrupt;
  bool S_software_interrupt;
  bool S_timer_interrupt;
  bool S_external_interrupt;

  bool is_br;
  bool br_taken;

  bool is_exception;
  bool is_csr;

  bool sim_end;
  uint64_t sim_time;
  bool is_io;
  int io_reg_idx;
  bool force_sync;

  const uint64_t INTERVAL_SIZE = 10000000;
  uint64_t interval_inst_count = 0;

  uint32_t current_bb_head_pc = 0;
  uint32_t current_bb_len = 0;

  std::unordered_map<uint32_t, uint32_t> global_pc_to_id;
  uint32_t next_bb_id = 1;
  std::vector<uint64_t> bbv_counts;
  std::ofstream bbv_file;

  void init(uint32_t reset_pc, const char *image, uint32_t size);
  void exec(const SimConfig &config);
  TraceInst step();
  
  bool va2pa(uint32_t &p_addr, uint32_t v_addr, uint32_t type);
  void RISCV();
  void FORCE_INLINE RV32IM();
  void FORCE_INLINE RV32A();
  void FORCE_INLINE RV32CSR();
  void exception(uint32_t trap_val);
  void store_data();
  uint32_t load_word(uint32_t addr) const;
  void store_word(uint32_t addr, uint32_t data);
  void bbv_commit();
  void bbv_init_file(const char *filename);
  void dump_bbv();
  void save_checkpoint(const std::string &filename);

  void restore_checkpoint(const std::string &filename);

  TraceInst current_trace;
};
