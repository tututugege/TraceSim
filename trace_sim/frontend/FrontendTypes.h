#pragma once
#include <cstdint>

enum class FetchStallReason : uint8_t {
    NONE = 0,
    ICACHE_MISS,
    FETCH_REDIRECT,
    BRANCH_MISPREDICT,
    LINE_BOUNDARY,
};

enum class BoundReason : uint8_t {
    NONE = 0,
    ICACHE_MISS,
    FETCH_REDIRECT,
    LINE_BOUNDARY,
    TRACE_SUPPLY,
    OTHER,
};
