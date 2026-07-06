#pragma once

#include "ob/common.h"

#include <cstdint>
#include <limits>

namespace ob::bitmap_engine {

using PriceIndex = std::uint32_t;
using LevelIndex = std::uint32_t;
using OrderIndex = std::uint32_t;
using BitmapWord = std::uint64_t;

inline constexpr std::uint32_t kWordBits = 64;
inline constexpr std::uint32_t kWordShift = 6; // 2^6 = 64

inline constexpr OrderIndex kInvalidOrderIndex =
    std::numeric_limits<OrderIndex>::max();

inline constexpr LevelIndex kInvalidLevelIndex =
    std::numeric_limits<LevelIndex>::max();

struct LadderConfig {
    ob::Price min_price = 1000;
    ob::Price max_price = 4000;
    ob::Price tick_size = 1;
};

} // namespace ob::bitmap_engine