#pragma once

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

struct PrefetcherAccessInfo {
    uint32_t pc = 0;
    uint32_t addr = 0;
    uint32_t line_size = 0;
    bool is_instruction = false;
    bool is_load = false;
    bool is_store = false;
    bool hit = false;
    bool miss = false;
};

struct PrefetchRequest {
    uint32_t addr = 0;
};

class CachePrefetcher {
public:
    virtual ~CachePrefetcher() = default;
    virtual const char *name() const = 0;
    virtual void on_access(const PrefetcherAccessInfo &info,
                           std::vector<PrefetchRequest> &requests) = 0;
};

class NullPrefetcher : public CachePrefetcher {
public:
    const char *name() const override { return "none"; }

    void on_access(const PrefetcherAccessInfo &,
                   std::vector<PrefetchRequest> &) override {}
};

class NextLinePrefetcher : public CachePrefetcher {
public:
    const char *name() const override { return "next-line"; }

    void on_access(const PrefetcherAccessInfo &info,
                   std::vector<PrefetchRequest> &requests) override {
        const uint32_t line_addr = info.addr & ~(info.line_size - 1);
        requests.push_back(PrefetchRequest{line_addr + info.line_size});
    }
};

class PcStridePrefetcher : public CachePrefetcher {
public:
    struct StrideEntry {
        uint32_t last_line_addr = 0;
        int32_t last_stride = 0;
        uint32_t confidence = 0;
        bool valid = false;
    };

    const char *name() const override { return "pc-stride"; }

    void on_access(const PrefetcherAccessInfo &info,
                   std::vector<PrefetchRequest> &requests) override {
        if (info.pc == 0 || info.line_size == 0) {
            return;
        }

        const uint32_t line_addr = info.addr & ~(info.line_size - 1);
        StrideEntry &entry = table[info.pc];
        if (!entry.valid) {
            entry.valid = true;
            entry.last_line_addr = line_addr;
            entry.last_stride = 0;
            entry.confidence = 0;
            return;
        }

        const int32_t stride =
            static_cast<int32_t>(line_addr) -
            static_cast<int32_t>(entry.last_line_addr);
        if (stride == 0) {
            return;
        }

        if (stride == entry.last_stride) {
            if (entry.confidence < kMaxConfidence) {
                entry.confidence++;
            }
        } else {
            entry.last_stride = stride;
            entry.confidence = 0;
        }

        entry.last_line_addr = line_addr;
        if (entry.confidence >= kTriggerConfidence) {
            requests.push_back(
                PrefetchRequest{static_cast<uint32_t>(
                    static_cast<int32_t>(line_addr) + stride)});
        }
    }

private:
    static constexpr uint32_t kTriggerConfidence = 2;
    static constexpr uint32_t kMaxConfidence = 3;
    std::unordered_map<uint32_t, StrideEntry> table;
};

inline std::unique_ptr<CachePrefetcher> make_null_prefetcher() {
    return std::make_unique<NullPrefetcher>();
}

inline std::unique_ptr<CachePrefetcher> make_next_line_prefetcher() {
    return std::make_unique<NextLinePrefetcher>();
}

inline std::unique_ptr<CachePrefetcher> make_pc_stride_prefetcher() {
    return std::make_unique<PcStridePrefetcher>();
}
