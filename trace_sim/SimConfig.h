#pragma once
#include <cstdint>

namespace TraceSimConfig {
// Branch Predictor Configuration
enum class BP_Type { GSHARE, PROBABILISTIC };
constexpr BP_Type BP_TYPE = BP_Type::PROBABILISTIC;
constexpr uint32_t BP_TARGET_ACCURACY =
    100; // Percentage (0-100), 100 = perfect BPU

// Pipeline Parameters
constexpr uint32_t FETCH_WIDTH = 8;
constexpr uint32_t ROB_SIZE = 2048;
constexpr uint32_t ALU_IQ_SIZE = 1024;
constexpr uint32_t LDU_IQ_SIZE = 512;
constexpr uint32_t STA_IQ_SIZE = 512;
constexpr uint32_t STD_IQ_SIZE = 512;
constexpr uint32_t BRU_IQ_SIZE = 512;

// Functional Unit Counts
constexpr uint32_t ALU_COUNT = 8;
constexpr uint32_t LDU_COUNT = 4;
constexpr uint32_t STA_COUNT = 4;
constexpr uint32_t STD_COUNT = 4;
constexpr uint32_t BRU_COUNT = 4;

// Execution Latencies (cycles)
constexpr uint32_t ALU_LATENCY = 1;
constexpr uint32_t LDU_LATENCY = 3;
constexpr uint32_t STA_LATENCY = 1;
constexpr uint32_t STD_LATENCY = 1;
constexpr uint32_t BRU_LATENCY = 1;

// Memory Access Latencies
constexpr uint32_t LSU_AGU_LATENCY =
    1; // Legacy, keep for now or replace everywhere
constexpr uint32_t LSU_MEM_LATENCY = 1; // Cache access latency (hit)

// Penalties
constexpr uint32_t BRANCH_MISPREDICT_PENALTY = 1;
constexpr uint32_t LLC_HIT_LATENCY =
    1; // Additional latency on L1 miss if LLC hits
constexpr uint32_t MEMORY_MISS_PENALTY =
    1; // Additional latency when LLC misses

// I-Cache Parameters
constexpr uint32_t ICACHE_SIZE = 16384; // 16 KB
constexpr uint32_t ICACHE_ASSOC = 4;
constexpr uint32_t ICACHE_LINE_SIZE = 64;

// D-Cache Parameters
constexpr uint32_t DCACHE_SIZE = 64 * 1024; // 32 KB
constexpr uint32_t DCACHE_ASSOC = 8;
constexpr uint32_t DCACHE_LINE_SIZE = 64;

// LLC (Shared by I$ and D$, i.e. unified L2)
constexpr uint32_t LLC_SIZE = 8388608; // 8 MB
constexpr uint32_t LLC_ASSOC = 16;
constexpr uint32_t LLC_LINE_SIZE = 64;

// Advanced Features
constexpr bool ENABLE_STLF = true; // Enable Store-to-Load Forwarding
constexpr bool ENABLE_MEM_DISAMBIGUATION =
    true; // Enable memory disambiguation stalls
// Extreme upper-bound mode: remove fetch bubbles from cache-line boundary and
// control-flow stops.
constexpr bool IGNORE_FETCH_BUBBLES = true;

// SimPoint / Checkpoint Parameters
constexpr uint64_t WARMUP_INSTRUCTIONS = 100000000; // 100M
constexpr uint64_t SAMPLE_INSTRUCTIONS = 100000000; // 100M
} // namespace TraceSimConfig
