#pragma once
#include <cstdint>

namespace TraceSimConfig {
    // Branch Predictor Configuration
    enum class BP_Type { GSHARE, PROBABILISTIC };
    constexpr BP_Type BP_TYPE = BP_Type::GSHARE;
    constexpr uint32_t BP_TARGET_ACCURACY = 95; // Percentage (0-100)
    
    // Pipeline Parameters
    constexpr uint32_t FETCH_WIDTH = 4;
    constexpr uint32_t ROB_SIZE = 128;
    constexpr uint32_t ALU_IQ_SIZE = 16;
    constexpr uint32_t LDU_IQ_SIZE = 8;
    constexpr uint32_t STA_IQ_SIZE = 8;
    constexpr uint32_t STD_IQ_SIZE = 8;
    constexpr uint32_t BRU_IQ_SIZE = 8;

    // Functional Unit Counts
    constexpr uint32_t ALU_COUNT = 4;
    constexpr uint32_t LDU_COUNT = 1;
    constexpr uint32_t STA_COUNT = 1;
    constexpr uint32_t STD_COUNT = 1;
    constexpr uint32_t BRU_COUNT = 1;

    // Execution Latencies (cycles)
    constexpr uint32_t ALU_LATENCY = 1;
    constexpr uint32_t LDU_LATENCY = 3; 
    constexpr uint32_t STA_LATENCY = 1;
    constexpr uint32_t STD_LATENCY = 1;
    constexpr uint32_t BRU_LATENCY = 1;
    
    // Memory Access Latencies
    constexpr uint32_t LSU_AGU_LATENCY = 1;     // Legacy, keep for now or replace everywhere
    constexpr uint32_t LSU_MEM_LATENCY = 2;     // Cache access latency (hit)

    // Penalties
    constexpr uint32_t BRANCH_MISPREDICT_PENALTY = 10;
    constexpr uint32_t ICACHE_MISS_PENALTY = 100;
    constexpr uint32_t DCACHE_MISS_PENALTY = 100;

    // I-Cache Parameters
    constexpr uint32_t ICACHE_SIZE = 16384;      // 16 KB
    constexpr uint32_t ICACHE_ASSOC = 4;
    constexpr uint32_t ICACHE_LINE_SIZE = 64;

    // D-Cache Parameters
    constexpr uint32_t DCACHE_SIZE = 32768;      // 32 KB
    constexpr uint32_t DCACHE_ASSOC = 8;
    constexpr uint32_t DCACHE_LINE_SIZE = 64;

    // Advanced Features
    constexpr bool ENABLE_STLF = true;             // Enable Store-to-Load Forwarding
    constexpr bool ENABLE_MEM_DISAMBIGUATION = true; // Enable memory disambiguation stalls

    // SimPoint / Checkpoint Parameters
    constexpr uint64_t WARMUP_INSTRUCTIONS = 100000000; // 100M
    constexpr uint64_t SAMPLE_INSTRUCTIONS = 100000000; // 100M
}
