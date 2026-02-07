#pragma once
#include <vector>
#include <cstdint>

class Cache {
public:
    struct Line {
        uint32_t tag;
        bool valid = false;
        uint64_t last_access = 0;
    };

    uint32_t size;         // in bytes
    uint32_t assoc;        // associativity
    uint32_t line_size;    // in bytes
    uint32_t num_sets;
    uint32_t offset_bits;
    uint32_t index_bits;
    uint32_t index_mask;

    std::vector<std::vector<Line>> sets;
    uint64_t access_count = 0;
    uint64_t hit_count = 0;

    Cache(uint32_t s, uint32_t a, uint32_t l) : size(s), assoc(a), line_size(l) {
        num_sets = size / (assoc * line_size);
        
        // Simple log2 for positive powers of 2
        auto get_bits = [](uint32_t n) {
            uint32_t bits = 0;
            while (n > 1) { n >>= 1; bits++; }
            return bits;
        };

        offset_bits = get_bits(line_size);
        index_bits = get_bits(num_sets);
        index_mask = num_sets - 1;
        sets.resize(num_sets, std::vector<Line>(assoc));
    }

    bool access(uint32_t addr, uint64_t current_cycle) {
        access_count++;
        uint32_t index = (addr >> offset_bits) & index_mask;
        uint32_t tag = addr >> (offset_bits + index_bits);

        auto &set = sets[index];
        // Check hit
        for (auto &line : set) {
            if (line.valid && line.tag == tag) {
                line.last_access = current_cycle;
                hit_count++;
                return true;
            }
        }

        // Miss - Replacement (LRU)
        uint64_t min_access = 0xFFFFFFFFFFFFFFFF;
        int target_idx = -1;
        for (int i = 0; i < (int)assoc; ++i) {
            if (!set[i].valid) {
                target_idx = i;
                break;
            }
            if (set[i].last_access < min_access) {
                min_access = set[i].last_access;
                target_idx = i;
            }
        }

        set[target_idx].valid = true;
        set[target_idx].tag = tag;
        set[target_idx].last_access = current_cycle;
        return false;
    }

    double get_hit_rate() const {
        if (access_count == 0) return 0.0;
        return (double)hit_count * 100.0 / access_count;
    }
};
