#pragma once

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Prefetcher.h"
#include "ReplacementPolicy.h"

class Cache {
public:
    struct Line {
        uint32_t tag = 0;
        bool valid = false;
        bool prefetched = false;
        bool used_by_demand = false;
        uint64_t last_access = 0;
        uint64_t insertion_time = 0;
        uint64_t fill_cycle = 0;
    };

    enum class AccessStatus {
        HIT,
        MISS_NEW,
        MISS_MERGED,
        MISS_BLOCKED,
        PREFETCH_DROPPED,
    };

    struct AccessResult {
        AccessStatus status = AccessStatus::HIT;
        uint64_t ready_cycle = 0;
        bool merged_with_prefetch = false;
    };

    struct InflightRequest {
        uint32_t block_addr = 0;
        uint64_t ready_cycle = 0;
        bool is_prefetch = false;
        bool has_demand_waiter = false;
    };

    uint32_t size;
    uint32_t assoc;
    uint32_t line_size;
    uint32_t num_sets;
    uint32_t offset_bits;
    uint32_t index_bits;
    uint32_t index_mask;

    std::vector<std::vector<Line>> sets;
    std::unordered_map<uint32_t, InflightRequest> inflight;
    uint64_t access_count = 0;
    uint64_t hit_count = 0;
    uint64_t miss_new_count = 0;
    uint64_t miss_merged_count = 0;
    uint64_t miss_blocked_count = 0;
    uint64_t prefetch_count = 0;
    uint64_t prefetch_hit_count = 0;
    uint64_t prefetch_fill_count = 0;
    uint64_t prefetch_dropped_count = 0;
    uint64_t prefetch_filtered_inflight_count = 0;
    uint64_t useful_prefetch_count = 0;
    uint64_t useless_prefetch_count = 0;
    uint64_t demand_merged_with_prefetch_count = 0;
    uint64_t late_prefetch_count = 0;
    uint32_t max_inflight = 16;

    std::unique_ptr<CacheReplacementPolicy> replacement_policy;
    std::unique_ptr<CachePrefetcher> prefetcher;

    Cache(uint32_t s, uint32_t a, uint32_t l,
          std::unique_ptr<CacheReplacementPolicy> repl =
              make_lru_replacement_policy(),
          std::unique_ptr<CachePrefetcher> pref = make_null_prefetcher(),
          uint32_t max_mshrs = 16)
        : size(s),
          assoc(a),
          line_size(l),
          replacement_policy(std::move(repl)),
          prefetcher(std::move(pref)),
          max_inflight(max_mshrs) {
        num_sets = size / (assoc * line_size);

        auto get_bits = [](uint32_t n) {
            uint32_t bits = 0;
            while (n > 1) {
                n >>= 1;
                bits++;
            }
            return bits;
        };

        offset_bits = get_bits(line_size);
        index_bits = get_bits(num_sets);
        index_mask = num_sets - 1;
        sets.resize(num_sets, std::vector<Line>(assoc));
    }

    uint32_t get_block_addr(uint32_t addr) const {
        return addr & ~(line_size - 1);
    }

    bool contains(uint32_t addr) const {
        const uint32_t index = (addr >> offset_bits) & index_mask;
        const uint32_t tag = addr >> (offset_bits + index_bits);
        const auto &set = sets[index];
        for (const auto &line : set) {
            if (line.valid && line.tag == tag) {
                return true;
            }
        }
        return false;
    }

    AccessResult request(uint32_t addr, uint64_t current_cycle,
                         bool is_prefetch = false) {
        if (is_prefetch) {
            prefetch_count++;
        } else {
            access_count++;
        }

        uint32_t index = (addr >> offset_bits) & index_mask;
        uint32_t tag = addr >> (offset_bits + index_bits);

        auto &set = sets[index];
        auto views = build_line_views(set);

        for (size_t way = 0; way < views.size(); ++way) {
            if (views[way].valid && views[way].tag == tag) {
                replacement_policy->on_cache_hit(index, way, current_cycle,
                                                 views);
                apply_line_views(set, views);
                if (is_prefetch) {
                    prefetch_hit_count++;
                } else {
                    hit_count++;
                    if (set[way].prefetched && !set[way].used_by_demand) {
                        useful_prefetch_count++;
                        set[way].used_by_demand = true;
                    }
                }
                return {AccessStatus::HIT, current_cycle, false};
            }
        }

        uint32_t block_addr = get_block_addr(addr);
        auto inflight_it = inflight.find(block_addr);
        if (inflight_it != inflight.end()) {
            if (is_prefetch) {
                prefetch_filtered_inflight_count++;
            } else {
                miss_merged_count++;
                if (inflight_it->second.is_prefetch) {
                    demand_merged_with_prefetch_count++;
                    late_prefetch_count++;
                }
            }
            if (!is_prefetch) {
                inflight_it->second.has_demand_waiter = true;
            }
            return {AccessStatus::MISS_MERGED, inflight_it->second.ready_cycle,
                    inflight_it->second.is_prefetch};
        }

        if (static_cast<uint32_t>(inflight.size()) >= max_inflight) {
            if (is_prefetch) {
                prefetch_dropped_count++;
                return {AccessStatus::PREFETCH_DROPPED, 0, false};
            }
            miss_blocked_count++;
            return {AccessStatus::MISS_BLOCKED, 0, false};
        }

        if (!is_prefetch) {
            miss_new_count++;
        }
        return {AccessStatus::MISS_NEW, 0, false};
    }

    void schedule_fill(uint32_t addr, uint64_t ready_cycle, bool is_prefetch) {
        uint32_t block_addr = get_block_addr(addr);
        auto [it, inserted] = inflight.emplace(
            block_addr,
            InflightRequest{block_addr, ready_cycle, is_prefetch, !is_prefetch});
        if (!inserted) {
            if (ready_cycle > it->second.ready_cycle) {
                it->second.ready_cycle = ready_cycle;
            }
            it->second.is_prefetch = it->second.is_prefetch && is_prefetch;
            it->second.has_demand_waiter =
                it->second.has_demand_waiter || !is_prefetch;
        }
    }

    void process_returns(uint64_t current_cycle) {
        std::vector<uint32_t> completed_blocks;
        completed_blocks.reserve(inflight.size());
        for (const auto &[block_addr, request] : inflight) {
            if (request.ready_cycle <= current_cycle) {
                completed_blocks.push_back(block_addr);
            }
        }

        for (uint32_t block_addr : completed_blocks) {
            const InflightRequest request = inflight.at(block_addr);
            fill_block(block_addr, current_cycle, request.is_prefetch,
                       request.has_demand_waiter);
            inflight.erase(block_addr);
        }
    }

    double get_hit_rate() const {
        if (access_count == 0) {
            return 0.0;
        }
        return static_cast<double>(hit_count) * 100.0 / access_count;
    }

private:
    std::vector<CacheReplacementPolicy::LineView>
    build_line_views(const std::vector<Line> &set) const {
        std::vector<CacheReplacementPolicy::LineView> views;
        views.reserve(set.size());
        for (const auto &line : set) {
            CacheReplacementPolicy::LineView view;
            view.valid = line.valid;
            view.tag = line.tag;
            view.last_access = line.last_access;
            view.insertion_time = line.insertion_time;
            views.push_back(view);
        }
        return views;
    }

    void apply_line_views(
        std::vector<Line> &set,
        const std::vector<CacheReplacementPolicy::LineView> &views) {
        for (size_t i = 0; i < set.size(); ++i) {
            set[i].valid = views[i].valid;
            set[i].tag = views[i].tag;
            set[i].last_access = views[i].last_access;
            set[i].insertion_time = views[i].insertion_time;
        }
    }

    void fill_block(uint32_t block_addr, uint64_t current_cycle,
                    bool is_prefetch_fill, bool has_demand_waiter) {
        uint32_t index = (block_addr >> offset_bits) & index_mask;
        uint32_t tag = block_addr >> (offset_bits + index_bits);

        auto &set = sets[index];
        auto views = build_line_views(set);
        size_t victim = replacement_policy->choose_victim(index, views);

        if (set[victim].valid && set[victim].prefetched &&
            !set[victim].used_by_demand) {
            useless_prefetch_count++;
        }

        views[victim].valid = true;
        views[victim].tag = tag;
        replacement_policy->on_cache_fill(index, victim, current_cycle, views);
        apply_line_views(set, views);
        set[victim].prefetched = is_prefetch_fill;
        set[victim].used_by_demand = has_demand_waiter || !is_prefetch_fill;
        set[victim].fill_cycle = current_cycle;
        if (is_prefetch_fill) {
            prefetch_fill_count++;
        }
    }
};
