#pragma once

#include <cstdint>
#include <deque>
#include <iostream>
#include <memory>

#include "../SimConfig.h"
#include "Cache.h"

class MemSubsystem {
public:
    struct CacheCounterSnapshot {
        uint64_t access = 0;
        uint64_t hit = 0;
        uint64_t miss_new = 0;
        uint64_t miss_merged = 0;
        uint64_t miss_blocked = 0;
        uint64_t prefetch = 0;
        uint64_t prefetch_hit = 0;
        uint64_t prefetch_fill = 0;
        uint64_t prefetch_dropped = 0;
        uint64_t prefetch_filtered_inflight = 0;
        uint64_t useful_prefetch = 0;
        uint64_t useless_prefetch = 0;
        uint64_t demand_merged_with_prefetch = 0;
        uint64_t late_prefetch = 0;
    };

    struct QueueCounterSnapshot {
        uint64_t filtered_hit = 0;
        uint64_t duplicate = 0;
        uint64_t drop = 0;
    };

    struct HierarchyAccessResult {
        Cache::AccessStatus l1_status = Cache::AccessStatus::HIT;
        Cache::AccessStatus llc_status = Cache::AccessStatus::HIT;
        bool blocked = false;
        bool l1_hit = false;
        bool llc_hit = false;
        bool dram_miss = false;
        uint64_t ready_cycle = 0;
    };

    MemSubsystem();

    uint32_t icache_line_size() const { return icache.line_size; }

    HierarchyAccessResult access_icache(uint32_t addr, uint64_t cycle);
    HierarchyAccessResult access_dcache_load(uint32_t addr, uint64_t cycle);
    void enqueue_prefetch(bool is_data, uint32_t pc, uint32_t addr, bool hit);
    void service_prefetch_queues(uint64_t cycle);
    void process_returns(uint64_t cycle);
    bool has_pending_work() const;
    void reset_baselines();
    void print_stats(std::ostream &os) const;

private:
    Cache icache;
    Cache dcache;
    Cache llc;

    std::deque<uint32_t> icache_prefetch_queue;
    std::deque<uint32_t> dcache_prefetch_queue;

    struct {
        CacheCounterSnapshot icache;
        CacheCounterSnapshot dcache;
        CacheCounterSnapshot llc;
        QueueCounterSnapshot icache_queue;
        QueueCounterSnapshot dcache_queue;
    } baselines;

    struct {
        uint64_t icache_filtered_hit = 0;
        uint64_t icache_queue_duplicate = 0;
        uint64_t icache_queue_drop = 0;
        uint64_t dcache_filtered_hit = 0;
        uint64_t dcache_queue_duplicate = 0;
        uint64_t dcache_queue_drop = 0;
    } prefetch_queue_stats;

    static CacheCounterSnapshot snapshot_cache(const Cache &cache);
    static void print_cache_stats(std::ostream &os, const char *name,
                                  const CacheCounterSnapshot &cur,
                                  const CacheCounterSnapshot &base);

    HierarchyAccessResult access_l1_hierarchy(Cache &l1, uint32_t addr,
                                              uint64_t cycle,
                                              uint32_t l1_hit_latency);
    uint64_t issue_prefetch_to_l1(Cache &l1, uint32_t addr, uint64_t cycle);
};
