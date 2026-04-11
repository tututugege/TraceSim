#include "Profiler.h"

Profiler::Profiler() {
    reset();
}

void Profiler::reset() {
    total_cycles_baseline = total_cycles;
    retired_insts_baseline = retired_insts;
    stats = {0, 0, 0, 0};
    topdown_counters.clear();
    topdown_counters[TopDownBucket::RETIRING] = 0;
    topdown_counters[TopDownBucket::FRONTEND_BOUND] = 0;
    topdown_counters[TopDownBucket::BACKEND_MEMORY_BOUND] = 0;
    topdown_counters[TopDownBucket::BACKEND_CORE_BOUND] = 0;
    topdown_counters[TopDownBucket::BAD_SPECULATION] = 0;
    topdown_counters[TopDownBucket::OTHER] = 0;
}

void Profiler::mark_cycle(uint64_t cur_cycle, bool retired, 
                       FetchStallReason fe_reason, bool fe_active, 
                       BackendStallReason be_reason) {
    total_cycles = cur_cycle;
    
    // Top-down Level 1/2 Attribution Priority Logic
    if (retired) {
        topdown_counters[TopDownBucket::RETIRING]++;
    } 
    else if (be_reason != BackendStallReason::NONE) {
        // 如果后端资源满或头部指令因为数据/执行依赖而停顿，归为 Backend Bound
        if (be_reason == BackendStallReason::MEMORY_WAIT) {
            topdown_counters[TopDownBucket::BACKEND_MEMORY_BOUND]++;
        } else {
            topdown_counters[TopDownBucket::BACKEND_CORE_BOUND]++;
        }
    }
    else if (fe_active) {
        // 如果是因为分支预测失败导致的停顿，单独归类
        if (fe_reason == FetchStallReason::BRANCH_MISPREDICT) {
            topdown_counters[TopDownBucket::BAD_SPECULATION]++;
        } else {
            topdown_counters[TopDownBucket::FRONTEND_BOUND]++;
        }
    } 
    else {
        // 其他情况（如流水线气泡排出）
        topdown_counters[TopDownBucket::OTHER]++;
    }
}

void Profiler::print_summary(uint32_t fetch_width, uint32_t rob_size) {
    uint64_t rel_inst = retired_insts - retired_insts_baseline;
    uint64_t rel_cycles = total_cycles - total_cycles_baseline;

    std::cout << "\n==============================================" << std::endl;
    std::cout << "          SIMULATION PROFILER REPORT          " << std::endl;
    std::cout << "==============================================" << std::endl;
    std::cout << "Execution Metrics:" << std::endl;
    std::cout << "  Retired Instructions: " << rel_inst << std::endl;
    std::cout << "  Total Cycles:         " << rel_cycles << std::endl;
    if (rel_cycles > 0) {
        std::cout << "  Overall IPC:          " << std::fixed << std::setprecision(2) 
                  << (double)rel_inst / rel_cycles << std::endl;
    }

    std::cout << "\nTop-Down Attribution Matrix:" << std::endl;
    if (rel_cycles > 0) {
        // 按照一定的逻辑顺序打印
        std::vector<TopDownBucket> print_order = {
            TopDownBucket::RETIRING,
            TopDownBucket::BAD_SPECULATION,
            TopDownBucket::FRONTEND_BOUND,
            TopDownBucket::BACKEND_MEMORY_BOUND,
            TopDownBucket::BACKEND_CORE_BOUND,
            TopDownBucket::OTHER
        };

        for (auto bucket : print_order) {
            uint64_t count = topdown_counters[bucket];
            double percent = (double)count * 100.0 / rel_cycles;
            std::cout << "  [" << std::left << std::setw(20) << bucket_to_string(bucket) << "]: " 
                      << std::right << std::setw(6) << std::fixed << std::setprecision(1) << percent << "% "
                      << "(" << count << " cycles)" << std::endl;
        }
    }

    std::cout << "\nBranch Prediction:" << std::endl;
    if (stats.total_branches > 0) {
        std::cout << "  Total Branches:       " << stats.total_branches << std::endl;
        std::cout << "  Accuracy:             " << std::fixed << std::setprecision(2)
                  << (double)stats.correct_branches * 100.0 / stats.total_branches << "%" << std::endl;
    }

    std::cout << "\nMemory Statistics:" << std::endl;
    std::cout << "  STLF Hits:            " << stats.stlf_hits << std::endl;
    std::cout << "  Memory Dep Stalls:    " << stats.mem_dep_stalls << std::endl;
    
    std::cout << "==============================================\n" << std::endl;
}

std::string Profiler::bucket_to_string(TopDownBucket bucket) {
    switch (bucket) {
        case TopDownBucket::RETIRING: return "Retiring";
        case TopDownBucket::FRONTEND_BOUND: return "Frontend Bound";
        case TopDownBucket::BACKEND_MEMORY_BOUND: return "Memory Bound";
        case TopDownBucket::BACKEND_CORE_BOUND: return "Core Bound";
        case TopDownBucket::BAD_SPECULATION: return "Bad Speculation";
        default: return "Other/Bubbles";
    }
}
