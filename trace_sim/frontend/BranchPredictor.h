#pragma once

#include <cstdlib>
#include <cstdint>
#include <ctime>
#include <memory>
#include <vector>

#include "../SimConfig.h"

class BranchPredictor {
public:
    struct Prediction {
        bool taken = false;
        uint64_t meta = 0;
    };

    virtual ~BranchPredictor() = default;
    virtual const char *name() const = 0;
    virtual Prediction predict(uint32_t pc) = 0;
    virtual void update(uint32_t pc, bool actual_taken, uint64_t meta) = 0;
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

    Prediction predict(uint32_t pc) override {
        uint32_t index = (pc ^ history) & history_mask;
        return Prediction{pht[index] >= 2, static_cast<uint64_t>(index)};
    }

    void update(uint32_t, bool actual_taken, uint64_t meta) override {
        uint32_t index = static_cast<uint32_t>(meta);
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
    const char *name() const override { return "probabilistic"; }

    Prediction predict(uint32_t) override { return Prediction{false, 0}; }

    void update(uint32_t, bool, uint64_t) override {}
};

class AlwaysTakenBranchPredictor : public BranchPredictor {
public:
    const char *name() const override { return "always-taken"; }
    Prediction predict(uint32_t) override { return Prediction{true, 0}; }
    void update(uint32_t, bool, uint64_t) override {}
};

class AlwaysNotTakenBranchPredictor : public BranchPredictor {
public:
    const char *name() const override { return "always-not-taken"; }
    Prediction predict(uint32_t) override { return Prediction{false, 0}; }
    void update(uint32_t, bool, uint64_t) override {}
};

class TemplateBranchPredictor : public BranchPredictor {
public:
    struct State {
        uint32_t last_pc = 0;
        bool valid = false;
    };

    const char *name() const override { return "template-bpu"; }

    Prediction predict(uint32_t pc) override {
        // Replace this with your real predictor lookup logic.
        const bool taken = state.valid && state.last_pc == pc;
        return Prediction{taken, 0};
    }

    void update(uint32_t pc, bool actual_taken, uint64_t) override {
        // Replace this with your real training logic.
        state.last_pc = pc;
        state.valid = actual_taken;
    }

private:
    State state;
};

inline bool branch_predictor_uses_oracle(TraceSimConfig::BP_Type type) {
    return type == TraceSimConfig::BP_Type::PROBABILISTIC ||
           type == TraceSimConfig::BP_Type::PERFECT;
}

inline bool oracle_branch_prediction(TraceSimConfig::BP_Type type,
                                     uint32_t target_accuracy,
                                     bool actual_taken) {
    static const bool seeded = []() {
        srand(static_cast<unsigned>(time(NULL)));
        return true;
    }();
    (void)seeded;
    switch (type) {
    case TraceSimConfig::BP_Type::PERFECT:
        return actual_taken;
    case TraceSimConfig::BP_Type::PROBABILISTIC:
        if (static_cast<uint32_t>(rand() % 100) < target_accuracy) {
            return actual_taken;
        }
        return !actual_taken;
    default:
        return false;
    }
}

inline std::unique_ptr<BranchPredictor>
make_branch_predictor(TraceSimConfig::BP_Type type) {
    switch (type) {
    case TraceSimConfig::BP_Type::GSHARE:
        return std::make_unique<GShareBranchPredictor>();
    case TraceSimConfig::BP_Type::ALWAYS_TAKEN:
        return std::make_unique<AlwaysTakenBranchPredictor>();
    case TraceSimConfig::BP_Type::ALWAYS_NOT_TAKEN:
        return std::make_unique<AlwaysNotTakenBranchPredictor>();
    case TraceSimConfig::BP_Type::PROBABILISTIC:
    case TraceSimConfig::BP_Type::PERFECT:
        return nullptr;
    default:
        return std::make_unique<GShareBranchPredictor>();
    }
}

inline std::unique_ptr<BranchPredictor> make_branch_predictor() {
    return make_branch_predictor(TraceSimConfig::BP_TYPE);
}
