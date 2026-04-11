#pragma once

#include <cstdlib>
#include <cstdint>
#include <ctime>
#include <memory>
#include <vector>

#include "../SimConfig.h"

class BranchPredictor {
public:
    virtual ~BranchPredictor() = default;
    virtual const char *name() const = 0;
    virtual bool predict(uint32_t pc, bool actual_taken) = 0;
    virtual void update(uint32_t pc, bool actual_taken) = 0;
};

class GShareBranchPredictor : public BranchPredictor {
public:
    std::vector<uint8_t> pht;
    uint32_t history = 0;
    uint32_t history_mask = 0;

    explicit GShareBranchPredictor(uint32_t history_bits = 12) {
        pht.resize(1U << history_bits, 2);
        history_mask = (1U << history_bits) - 1;
    }

    const char *name() const override { return "gshare"; }

    bool predict(uint32_t pc, bool) override {
        uint32_t index = (pc ^ history) & history_mask;
        return pht[index] >= 2;
    }

    void update(uint32_t pc, bool actual_taken) override {
        uint32_t index = (pc ^ history) & history_mask;
        if (actual_taken) {
            if (pht[index] < 3) {
                pht[index]++;
            }
        } else if (pht[index] > 0) {
            pht[index]--;
        }
        history = ((history << 1) | static_cast<uint32_t>(actual_taken)) &
                  history_mask;
    }
};

class ProbabilisticBranchPredictor : public BranchPredictor {
public:
    uint32_t target_accuracy = 0;

    explicit ProbabilisticBranchPredictor(uint32_t acc) : target_accuracy(acc) {
        srand(static_cast<unsigned>(time(NULL)));
    }

    const char *name() const override { return "probabilistic"; }

    bool predict(uint32_t, bool actual_taken) override {
        if (static_cast<uint32_t>(rand() % 100) < target_accuracy) {
            return actual_taken;
        }
        return !actual_taken;
    }

    void update(uint32_t, bool) override {}
};

class AlwaysTakenBranchPredictor : public BranchPredictor {
public:
    const char *name() const override { return "always-taken"; }
    bool predict(uint32_t, bool) override { return true; }
    void update(uint32_t, bool) override {}
};

class AlwaysNotTakenBranchPredictor : public BranchPredictor {
public:
    const char *name() const override { return "always-not-taken"; }
    bool predict(uint32_t, bool) override { return false; }
    void update(uint32_t, bool) override {}
};

class PerfectBranchPredictor : public BranchPredictor {
public:
    const char *name() const override { return "perfect"; }
    bool predict(uint32_t, bool actual_taken) override { return actual_taken; }
    void update(uint32_t, bool) override {}
};

inline std::unique_ptr<BranchPredictor> make_branch_predictor() {
    switch (TraceSimConfig::BP_TYPE) {
    case TraceSimConfig::BP_Type::GSHARE:
        return std::make_unique<GShareBranchPredictor>();
    case TraceSimConfig::BP_Type::PROBABILISTIC:
    default:
        return std::make_unique<ProbabilisticBranchPredictor>(
            TraceSimConfig::BP_TARGET_ACCURACY);
    }
}

