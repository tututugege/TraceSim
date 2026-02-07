#pragma once
#include <vector>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include "SimConfig.h"

// Abstract Base Class for Branch Predictors
class BranchPredictor {
public:
    virtual ~BranchPredictor() = default;
    virtual bool predict(uint32_t pc, bool actual_taken) = 0;
    virtual void update(uint32_t pc, bool actual_taken) = 0;
};

class GShareBranchPredictor : public BranchPredictor {
public:
    std::vector<uint8_t> pht;
    uint32_t history = 0;
    uint32_t history_mask;

    GShareBranchPredictor(uint32_t history_bits = 12) {
        pht.resize(1 << history_bits, 2); // Initialized to weakly taken
        history_mask = (1 << history_bits) - 1;
    }

    bool predict(uint32_t pc, bool actual_taken) override {
        uint32_t index = (pc ^ history) & history_mask;
        return pht[index] >= 2;
    }

    void update(uint32_t pc, bool actual_taken) override {
        uint32_t index = (pc ^ history) & history_mask;
        if (actual_taken) {
            if (pht[index] < 3) pht[index]++;
        } else {
            if (pht[index] > 0) pht[index]--;
        }
        history = ((history << 1) | actual_taken) & history_mask;
    }
};

class ProbabilisticBranchPredictor : public BranchPredictor {
public:
    uint32_t target_accuracy;

    ProbabilisticBranchPredictor(uint32_t acc) : target_accuracy(acc) {
        srand(time(NULL));
    }

    bool predict(uint32_t pc, bool actual_taken) override {
        uint32_t r = rand() % 100;
        if (r < target_accuracy) {
            return actual_taken; // Predict correctly
        } else {
            return !actual_taken; // Predict incorrectly
        }
    }

    void update(uint32_t pc, bool actual_taken) override {}
};
