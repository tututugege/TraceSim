#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "../SimConfig.h"

class CacheReplacementPolicy {
public:
    struct LineView {
        bool valid = false;
        uint32_t tag = 0;
        uint64_t last_access = 0;
        uint64_t insertion_time = 0;
    };

    virtual ~CacheReplacementPolicy() = default;
    virtual const char *name() const = 0;
    virtual size_t choose_victim(
        uint32_t set_index,
        const std::vector<LineView> &set_lines) const = 0;
    virtual void on_cache_hit(
        uint32_t set_index,
        size_t way,
        uint64_t cycle,
        std::vector<LineView> &set_lines) const = 0;
    virtual void on_cache_fill(
        uint32_t set_index,
        size_t way,
        uint64_t cycle,
        std::vector<LineView> &set_lines) const = 0;
};

class LruReplacementPolicy : public CacheReplacementPolicy {
public:
    const char *name() const override { return "lru"; }

    size_t choose_victim(
        uint32_t,
        const std::vector<LineView> &set_lines) const override {
        size_t victim = 0;
        uint64_t min_access = UINT64_MAX;
        for (size_t i = 0; i < set_lines.size(); ++i) {
            if (!set_lines[i].valid) {
                return i;
            }
            if (set_lines[i].last_access < min_access) {
                min_access = set_lines[i].last_access;
                victim = i;
            }
        }
        return victim;
    }

    void on_cache_hit(
        uint32_t,
        size_t way,
        uint64_t cycle,
        std::vector<LineView> &set_lines) const override {
        set_lines[way].last_access = cycle;
    }

    void on_cache_fill(
        uint32_t,
        size_t way,
        uint64_t cycle,
        std::vector<LineView> &set_lines) const override {
        set_lines[way].last_access = cycle;
        set_lines[way].insertion_time = cycle;
    }
};

class FifoReplacementPolicy : public CacheReplacementPolicy {
public:
    const char *name() const override { return "fifo"; }

    size_t choose_victim(
        uint32_t,
        const std::vector<LineView> &set_lines) const override {
        size_t victim = 0;
        uint64_t oldest_insert = UINT64_MAX;
        for (size_t i = 0; i < set_lines.size(); ++i) {
            if (!set_lines[i].valid) {
                return i;
            }
            if (set_lines[i].insertion_time < oldest_insert) {
                oldest_insert = set_lines[i].insertion_time;
                victim = i;
            }
        }
        return victim;
    }

    void on_cache_hit(
        uint32_t,
        size_t,
        uint64_t,
        std::vector<LineView> &) const override {}

    void on_cache_fill(
        uint32_t,
        size_t way,
        uint64_t cycle,
        std::vector<LineView> &set_lines) const override {
        set_lines[way].last_access = cycle;
        set_lines[way].insertion_time = cycle;
    }
};

class TemplateReplacementPolicy : public CacheReplacementPolicy {
public:
    const char *name() const override { return "template-replacement"; }

    size_t choose_victim(
        uint32_t,
        const std::vector<LineView> &set_lines) const override {
        for (size_t i = 0; i < set_lines.size(); ++i) {
            if (!set_lines[i].valid) {
                return i;
            }
        }

        // Replace this with your own victim-selection logic.
        return 0;
    }

    void on_cache_hit(
        uint32_t,
        size_t way,
        uint64_t cycle,
        std::vector<LineView> &set_lines) const override {
        // Replace this with your own metadata update logic.
        set_lines[way].last_access = cycle;
    }

    void on_cache_fill(
        uint32_t,
        size_t way,
        uint64_t cycle,
        std::vector<LineView> &set_lines) const override {
        // Replace this with your own fill-time metadata update logic.
        set_lines[way].last_access = cycle;
        set_lines[way].insertion_time = cycle;
    }
};

inline std::unique_ptr<CacheReplacementPolicy> make_lru_replacement_policy() {
    return std::make_unique<LruReplacementPolicy>();
}

inline std::unique_ptr<CacheReplacementPolicy> make_fifo_replacement_policy() {
    return std::make_unique<FifoReplacementPolicy>();
}

inline std::unique_ptr<CacheReplacementPolicy>
make_replacement_policy(TraceSimConfig::ReplacementPolicyType type) {
    switch (type) {
    case TraceSimConfig::ReplacementPolicyType::FIFO:
        return make_fifo_replacement_policy();
    case TraceSimConfig::ReplacementPolicyType::LRU:
    default:
        return make_lru_replacement_policy();
    }
}
