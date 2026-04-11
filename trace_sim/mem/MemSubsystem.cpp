#include "MemSubsystem.h"

#include <iomanip>
#include <vector>

namespace {

constexpr uint32_t kIcacheHitLatency = 0;

}

MemSubsystem::CacheCounterSnapshot
MemSubsystem::snapshot_cache(const Cache &cache) {
    CacheCounterSnapshot snapshot;
    snapshot.access = cache.access_count;
    snapshot.hit = cache.hit_count;
    snapshot.miss_new = cache.miss_new_count;
    snapshot.miss_merged = cache.miss_merged_count;
    snapshot.miss_blocked = cache.miss_blocked_count;
    snapshot.prefetch = cache.prefetch_count;
    snapshot.prefetch_hit = cache.prefetch_hit_count;
    snapshot.prefetch_fill = cache.prefetch_fill_count;
    snapshot.prefetch_dropped = cache.prefetch_dropped_count;
    snapshot.prefetch_filtered_inflight = cache.prefetch_filtered_inflight_count;
    snapshot.useful_prefetch = cache.useful_prefetch_count;
    snapshot.useless_prefetch = cache.useless_prefetch_count;
    snapshot.demand_merged_with_prefetch = cache.demand_merged_with_prefetch_count;
    snapshot.late_prefetch = cache.late_prefetch_count;
    return snapshot;
}

MemSubsystem::MemSubsystem()
    : icache(TraceSimConfig::ICACHE_SIZE, TraceSimConfig::ICACHE_ASSOC,
             TraceSimConfig::ICACHE_LINE_SIZE,
             make_replacement_policy(TraceSimConfig::ICACHE_REPLACEMENT),
             make_prefetcher(TraceSimConfig::ICACHE_PREFETCHER),
             TraceSimConfig::ICACHE_MSHR_SIZE),
      dcache(TraceSimConfig::DCACHE_SIZE, TraceSimConfig::DCACHE_ASSOC,
             TraceSimConfig::DCACHE_LINE_SIZE,
             make_replacement_policy(TraceSimConfig::DCACHE_REPLACEMENT),
             make_prefetcher(TraceSimConfig::DCACHE_PREFETCHER),
             TraceSimConfig::DCACHE_MSHR_SIZE),
      llc(TraceSimConfig::LLC_SIZE, TraceSimConfig::LLC_ASSOC,
          TraceSimConfig::LLC_LINE_SIZE,
          make_replacement_policy(TraceSimConfig::LLC_REPLACEMENT),
          make_prefetcher(TraceSimConfig::LLC_PREFETCHER),
          TraceSimConfig::LLC_MSHR_SIZE) {}

MemSubsystem::HierarchyAccessResult
MemSubsystem::access_l1_hierarchy(Cache &l1, uint32_t addr, uint64_t cycle,
                                  uint32_t l1_hit_latency) {
    HierarchyAccessResult result;
    Cache::AccessResult l1_res = l1.request(addr, cycle, false);
    result.l1_status = l1_res.status;
    result.l1_hit = l1_res.status == Cache::AccessStatus::HIT;
    result.ready_cycle = cycle + l1_hit_latency;

    if (l1_res.status == Cache::AccessStatus::MISS_BLOCKED) {
        result.blocked = true;
        return result;
    }

    if (result.l1_hit) {
        return result;
    }

    if (l1_res.status == Cache::AccessStatus::MISS_MERGED) {
        result.ready_cycle =
            std::max(cycle + static_cast<uint64_t>(l1_hit_latency),
                     l1_res.ready_cycle);
        return result;
    }

    Cache::AccessResult llc_res = llc.request(addr, cycle, false);
    result.llc_status = llc_res.status;
    if (llc_res.status == Cache::AccessStatus::MISS_BLOCKED) {
        result.blocked = true;
        return result;
    }

    result.llc_hit = llc_res.status == Cache::AccessStatus::HIT;
    result.dram_miss = llc_res.status == Cache::AccessStatus::MISS_NEW;

    uint64_t llc_ready = cycle + TraceSimConfig::LLC_HIT_LATENCY;
    if (llc_res.status == Cache::AccessStatus::MISS_MERGED) {
        llc_ready = llc_res.ready_cycle;
    } else if (llc_res.status == Cache::AccessStatus::MISS_NEW) {
        llc_ready = cycle + TraceSimConfig::LLC_HIT_LATENCY +
                    TraceSimConfig::MEMORY_MISS_PENALTY;
        llc.schedule_fill(addr, llc_ready, false);
    }

    result.ready_cycle =
        cycle + static_cast<uint64_t>(l1_hit_latency) + (llc_ready - cycle);
    l1.schedule_fill(addr, result.ready_cycle, false);
    return result;
}

MemSubsystem::HierarchyAccessResult
MemSubsystem::access_icache(uint32_t addr, uint64_t cycle) {
    return access_l1_hierarchy(icache, addr, cycle, kIcacheHitLatency);
}

MemSubsystem::HierarchyAccessResult
MemSubsystem::access_dcache_load(uint32_t addr, uint64_t cycle) {
    return access_l1_hierarchy(dcache, addr, cycle,
                               TraceSimConfig::LDU_LATENCY);
}

void MemSubsystem::enqueue_prefetch(bool is_data, uint32_t pc, uint32_t addr,
                                    bool hit) {
    Cache &l1 = is_data ? dcache : icache;
    std::vector<PrefetchRequest> requests;
    PrefetcherAccessInfo info;
    info.pc = pc;
    info.addr = addr;
    info.line_size = l1.line_size;
    info.is_instruction = !is_data;
    info.is_load = is_data;
    info.hit = hit;
    info.miss = !hit;
    l1.prefetcher->on_access(info, requests);

    auto &queue = is_data ? dcache_prefetch_queue : icache_prefetch_queue;
    const uint32_t queue_limit = is_data
                                     ? TraceSimConfig::DCACHE_PREFETCH_QUEUE_SIZE
                                     : TraceSimConfig::ICACHE_PREFETCH_QUEUE_SIZE;
    uint64_t &filtered_hit = is_data
                                 ? prefetch_queue_stats.dcache_filtered_hit
                                 : prefetch_queue_stats.icache_filtered_hit;
    uint64_t &queue_dup = is_data
                              ? prefetch_queue_stats.dcache_queue_duplicate
                              : prefetch_queue_stats.icache_queue_duplicate;
    uint64_t &queue_drop = is_data
                               ? prefetch_queue_stats.dcache_queue_drop
                               : prefetch_queue_stats.icache_queue_drop;

    for (const PrefetchRequest &request : requests) {
        const uint32_t pf_addr = request.addr;
        const uint32_t pf_block = l1.get_block_addr(pf_addr);

        if (l1.contains(pf_addr)) {
            filtered_hit++;
            continue;
        }

        bool duplicate = false;
        for (uint32_t queued_addr : queue) {
            if (l1.get_block_addr(queued_addr) == pf_block) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) {
            queue_dup++;
            continue;
        }
        if (queue.size() >= queue_limit) {
            queue_drop++;
            continue;
        }
        queue.push_back(pf_addr);
    }
}

uint64_t MemSubsystem::issue_prefetch_to_l1(Cache &l1, uint32_t addr,
                                            uint64_t cycle) {
    Cache::AccessResult l1_res = l1.request(addr, cycle, true);
    switch (l1_res.status) {
    case Cache::AccessStatus::HIT:
    case Cache::AccessStatus::MISS_MERGED:
    case Cache::AccessStatus::MISS_BLOCKED:
    case Cache::AccessStatus::PREFETCH_DROPPED:
        return l1_res.ready_cycle;
    case Cache::AccessStatus::MISS_NEW:
        break;
    }

    Cache::AccessResult llc_res = llc.request(addr, cycle, true);
    switch (llc_res.status) {
    case Cache::AccessStatus::HIT: {
        const uint64_t ready_cycle = cycle + TraceSimConfig::LLC_HIT_LATENCY;
        l1.schedule_fill(addr, ready_cycle, true);
        return ready_cycle;
    }
    case Cache::AccessStatus::MISS_MERGED:
        l1.schedule_fill(addr, llc_res.ready_cycle, true);
        return llc_res.ready_cycle;
    case Cache::AccessStatus::MISS_NEW: {
        const uint64_t ready_cycle = cycle + TraceSimConfig::LLC_HIT_LATENCY +
                                     TraceSimConfig::MEMORY_MISS_PENALTY;
        llc.schedule_fill(addr, ready_cycle, true);
        l1.schedule_fill(addr, ready_cycle, true);
        return ready_cycle;
    }
    case Cache::AccessStatus::MISS_BLOCKED:
    case Cache::AccessStatus::PREFETCH_DROPPED:
        return 0;
    }
    return 0;
}

void MemSubsystem::service_prefetch_queues(uint64_t cycle) {
    for (uint32_t i = 0;
         i < TraceSimConfig::ICACHE_PREFETCH_ISSUE_PER_CYCLE &&
         !icache_prefetch_queue.empty();
         ++i) {
        const uint32_t pf_addr = icache_prefetch_queue.front();
        icache_prefetch_queue.pop_front();
        (void)issue_prefetch_to_l1(icache, pf_addr, cycle);
    }

    for (uint32_t i = 0;
         i < TraceSimConfig::DCACHE_PREFETCH_ISSUE_PER_CYCLE &&
         !dcache_prefetch_queue.empty();
         ++i) {
        const uint32_t pf_addr = dcache_prefetch_queue.front();
        dcache_prefetch_queue.pop_front();
        (void)issue_prefetch_to_l1(dcache, pf_addr, cycle);
    }
}

void MemSubsystem::process_returns(uint64_t cycle) {
    icache.process_returns(cycle);
    dcache.process_returns(cycle);
    llc.process_returns(cycle);
}

bool MemSubsystem::has_pending_work() const {
    return !icache_prefetch_queue.empty() || !dcache_prefetch_queue.empty();
}

void MemSubsystem::reset_baselines() {
    baselines.icache = snapshot_cache(icache);
    baselines.dcache = snapshot_cache(dcache);
    baselines.llc = snapshot_cache(llc);
    baselines.icache_queue.filtered_hit = prefetch_queue_stats.icache_filtered_hit;
    baselines.icache_queue.duplicate = prefetch_queue_stats.icache_queue_duplicate;
    baselines.icache_queue.drop = prefetch_queue_stats.icache_queue_drop;
    baselines.dcache_queue.filtered_hit = prefetch_queue_stats.dcache_filtered_hit;
    baselines.dcache_queue.duplicate = prefetch_queue_stats.dcache_queue_duplicate;
    baselines.dcache_queue.drop = prefetch_queue_stats.dcache_queue_drop;
}

void MemSubsystem::print_cache_stats(std::ostream &os, const char *name,
                                     const CacheCounterSnapshot &cur,
                                     const CacheCounterSnapshot &base) {
    const uint64_t access = cur.access - base.access;
    const uint64_t hit = cur.hit - base.hit;
    const uint64_t miss_new = cur.miss_new - base.miss_new;
    const uint64_t miss_merged = cur.miss_merged - base.miss_merged;
    const uint64_t miss_blocked = cur.miss_blocked - base.miss_blocked;
    const uint64_t prefetch = cur.prefetch - base.prefetch;
    const uint64_t prefetch_hit = cur.prefetch_hit - base.prefetch_hit;
    const uint64_t prefetch_fill = cur.prefetch_fill - base.prefetch_fill;
    const uint64_t prefetch_dropped =
        cur.prefetch_dropped - base.prefetch_dropped;
    const uint64_t filtered_inflight =
        cur.prefetch_filtered_inflight - base.prefetch_filtered_inflight;
    const uint64_t useful = cur.useful_prefetch - base.useful_prefetch;
    const uint64_t useless = cur.useless_prefetch - base.useless_prefetch;
    const uint64_t merged_with_prefetch =
        cur.demand_merged_with_prefetch - base.demand_merged_with_prefetch;
    const uint64_t late = cur.late_prefetch - base.late_prefetch;

    os << "  " << name << ": hit/access=" << hit << "/" << access
       << ", miss_new=" << miss_new
       << ", miss_merged=" << miss_merged
       << ", miss_blocked=" << miss_blocked << std::endl;
    os << "    prefetch: issued=" << prefetch
       << ", filtered_inflight=" << filtered_inflight
       << ", hit=" << prefetch_hit
       << ", dropped=" << prefetch_dropped
       << ", fills=" << prefetch_fill
       << ", useful=" << useful
       << ", useless=" << useless
       << ", merged_with_prefetch=" << merged_with_prefetch
       << ", late=" << late << std::endl;
}

void MemSubsystem::print_stats(std::ostream &os) const {
    const CacheCounterSnapshot cur_icache = snapshot_cache(icache);
    const CacheCounterSnapshot cur_dcache = snapshot_cache(dcache);
    const CacheCounterSnapshot cur_llc = snapshot_cache(llc);

    os << "Raw Cache Stats:" << std::endl;
    print_cache_stats(os, "I-Cache", cur_icache, baselines.icache);
    print_cache_stats(os, "D-Cache", cur_dcache, baselines.dcache);
    print_cache_stats(os, "LLC", cur_llc, baselines.llc);

    os << "Prefetch Queue Stats:" << std::endl;
    os << "  I-Cache: filtered_hit="
       << (prefetch_queue_stats.icache_filtered_hit -
           baselines.icache_queue.filtered_hit)
       << ", queue_dup="
       << (prefetch_queue_stats.icache_queue_duplicate -
           baselines.icache_queue.duplicate)
       << ", queue_drop="
       << (prefetch_queue_stats.icache_queue_drop -
           baselines.icache_queue.drop) << std::endl;
    os << "  D-Cache: filtered_hit="
       << (prefetch_queue_stats.dcache_filtered_hit -
           baselines.dcache_queue.filtered_hit)
       << ", queue_dup="
       << (prefetch_queue_stats.dcache_queue_duplicate -
           baselines.dcache_queue.duplicate)
       << ", queue_drop="
       << (prefetch_queue_stats.dcache_queue_drop -
           baselines.dcache_queue.drop) << std::endl;
}
