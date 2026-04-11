#pragma once
#include <cstdint>

namespace TraceSimConfig {

// =============================================================================
// 1. 分支预测器配置 (Branch Predictor)
// =============================================================================
enum class BP_Type { GSHARE, PROBABILISTIC };
constexpr BP_Type BP_TYPE = BP_Type::GSHARE;
constexpr uint32_t BP_TARGET_ACCURACY = 95;  // 用于概率模型的准确率

// =============================================================================
// 2. 流水线带宽与规模 (Pipeline Width & Size)
// =============================================================================
constexpr uint32_t FETCH_WIDTH    = 8;  // 前端取指带宽 (Fetch)
constexpr uint32_t DISPATCH_WIDTH = 6;  // 后端派发/重命名带宽 (Dispatch/Rename)
constexpr uint32_t COMMIT_WIDTH   = 6;  // 退休带宽 (Retire)

constexpr uint32_t ROB_SIZE       = 256; // 重排序缓存大小 (Reorder Buffer)
constexpr uint32_t INST_BUFFER_SIZE = 64; // 前端指令缓冲区

// 发射队列大小 (Issue Queue Sizes)
constexpr uint32_t ALU_IQ_SIZE    = 64;
constexpr uint32_t LDU_IQ_SIZE    = 48;
constexpr uint32_t STA_IQ_SIZE    = 32;
constexpr uint32_t STD_IQ_SIZE    = 32;
constexpr uint32_t BRU_IQ_SIZE    = 32;

// =============================================================================
// 3. 执行单元配比 (Functional Unit Counts)
// =============================================================================
constexpr uint32_t ALU_COUNT = 4;
constexpr uint32_t LDU_COUNT = 2;
constexpr uint32_t STA_COUNT = 2;
constexpr uint32_t STD_COUNT = 2;
constexpr uint32_t BRU_COUNT = 1;

// =============================================================================
// 4. 执行延迟 (Execution Latency in cycles)
// =============================================================================
constexpr uint32_t ALU_LATENCY   = 1;
constexpr uint32_t LDU_LATENCY   = 3; // L1 Hit (Effective latency)
constexpr uint32_t BRU_LATENCY   = 1;
constexpr uint32_t STA_LATENCY   = 1;
constexpr uint32_t STD_LATENCY   = 1;

// 惩罚与长延迟
constexpr uint32_t BRANCH_MISPREDICT_PENALTY = 10; // 误预测清除流水线
constexpr uint32_t FETCH_REDIRECT_LATENCY    = 1;  // 正确预测的 Taken 分支/跳转在 BTB 中的刷新延迟
constexpr uint32_t LLC_HIT_LATENCY     = 12;  // L2 访问延迟 (L1 Miss, L2 Hit)
constexpr uint32_t MEMORY_MISS_PENALTY = 120; // DRAM 访问延迟 (L2 Miss)

// =============================================================================
// 5. 存储层次参数 (Cache Parameters)
// =============================================================================
// I-Cache: 32 KB, 8-way
constexpr uint32_t ICACHE_SIZE      = 32768; 
constexpr uint32_t ICACHE_ASSOC     = 8;
constexpr uint32_t ICACHE_LINE_SIZE = 64;

// D-Cache: 32 KB, 8-way
constexpr uint32_t DCACHE_SIZE      = 32768; 
constexpr uint32_t DCACHE_ASSOC     = 8;
constexpr uint32_t DCACHE_LINE_SIZE = 64;

// L2 Cache (Unified): 512 KB, 8-way
constexpr uint32_t LLC_SIZE       = 524288; 
constexpr uint32_t LLC_ASSOC      = 8;
constexpr uint32_t LLC_LINE_SIZE  = 64;

// =============================================================================
// 6. 访存依赖模型 (Memory Dependency)
// =============================================================================
enum class MemDepModel {
    CONSERVATIVE_STA_VISIBLE, 
    ORACLE_STLF               
};
constexpr MemDepModel MEM_DEP_MODEL = MemDepModel::ORACLE_STLF;
constexpr bool ENABLE_STLF = true;

// =============================================================================
// 7. 统计与采样 (Profiling & Sampling)
// =============================================================================
constexpr bool ENABLE_DEPENDENT_LOAD_PROFILING = true;
constexpr uint64_t WARMUP_INSTRUCTIONS = 1000000;
constexpr uint64_t SAMPLE_INSTRUCTIONS = 10000000;

} // namespace TraceSimConfig
