#pragma once
#include <cstdint>
#include "FrontendTypes.h"

class TraceSim; // Forward declaration

class Frontend {
public:
    explicit Frontend(TraceSim& simulator) : sim(simulator) {}

    void fetch_stage();

private:
    TraceSim& sim;
};
