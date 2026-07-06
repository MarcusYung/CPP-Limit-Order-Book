// generator.cpp
#include "generator.h"
#include <algorithm>

namespace MySTLOB {

OrderGenerator::OrderGenerator(Price mid_price, Price tick_size, uint64_t seed)
    : mid_price_(mid_price)
    , tick_size_(tick_size)
    , next_order_id_(1)
    , rng_(seed)
    , price_offset_dist_(0.0, 5.0)    // std dev of 5 ticks from mid
    , quantity_dist_(1, 500)           // 1 to 500 units
    , action_dist_(0.0, 1.0)
    , side_dist_(0.0, 1.0)
    , drift_dist_(0.0, 0.5)           // small random walk on mid price
{
    live_orders_.reserve(10000);
}

GeneratedAction OrderGenerator::next() {
    // Drift the mid price slightly (random walk)
    double drift = drift_dist_(rng_);
    int64_t tick_drift = static_cast<int64_t>(std::round(drift));
    int64_t new_mid = static_cast<int64_t>(mid_price_) + tick_drift * static_cast<int64_t>(tick_size_);
    if (new_mid > 0) mid_price_ = static_cast<Price>(new_mid);

    double action_roll = action_dist_(rng_);

    // 70% add, 20% cancel, 10% replace
    if (action_roll < 0.70 || live_orders_.empty()) {
        // ADD order
        Side side = (side_dist_(rng_) < 0.5) ? Side::BUY : Side::SELL;

        // Price offset: positive means further from mid (more passive)
        // Negative means crossing (aggressive)
        double raw_offset = price_offset_dist_(rng_);
        int64_t tick_offset = static_cast<int64_t>(std::round(std::abs(raw_offset)));

        Price price;
        if (side == Side::BUY) {
            // Buy orders placed below mid (passive) or at/above mid (aggressive)
            int64_t p = static_cast<int64_t>(mid_price_) - tick_offset * static_cast<int64_t>(tick_size_);
            // ~15% chance of being aggressive (crossing the spread)
            if (raw_offset < -1.0) {
                p = static_cast<int64_t>(mid_price_) + tick_offset * static_cast<int64_t>(tick_size_);
            }
            price = static_cast<Price>(std::max(p, static_cast<int64_t>(tick_size_)));
        } else {
            // Sell orders placed above mid (passive) or at/below mid (aggressive)
            int64_t p = static_cast<int64_t>(mid_price_) + tick_offset * static_cast<int64_t>(tick_size_);
            if (raw_offset < -1.0) {
                p = static_cast<int64_t>(mid_price_) - tick_offset * static_cast<int64_t>(tick_size_);
            }
            price = static_cast<Price>(std::max(p, static_cast<int64_t>(tick_size_)));
        }

        Quantity qty = quantity_dist_(rng_);
        OrderId id = next_order_id_++;
        live_orders_.push_back(id);

        return GeneratedAction{ ActionType::ADD, id, qty, price, side, 0, 0 };

    } else if (action_roll < 0.90) {
        // CANCEL order
        std::uniform_int_distribution<size_t> idx_dist(0, live_orders_.size() - 1);
        size_t idx = idx_dist(rng_);
        OrderId id = live_orders_[idx];
        // Swap-and-pop removal
        live_orders_[idx] = live_orders_.back();
        live_orders_.pop_back();

        return GeneratedAction{ ActionType::CANCEL, id, 0, 0, Side::BUY, 0, 0 };

    } else {
        // REPLACE order
        std::uniform_int_distribution<size_t> idx_dist(0, live_orders_.size() - 1);
        size_t idx = idx_dist(rng_);
        OrderId id = live_orders_[idx];

        Quantity new_qty = quantity_dist_(rng_);
        double raw_offset = price_offset_dist_(rng_);
        int64_t tick_offset = static_cast<int64_t>(std::round(std::abs(raw_offset)));
        // Random new price near mid
        Price new_price;
        if (side_dist_(rng_) < 0.5) {
            int64_t p = static_cast<int64_t>(mid_price_) - tick_offset * static_cast<int64_t>(tick_size_);
            new_price = static_cast<Price>(std::max(p, static_cast<int64_t>(tick_size_)));
        } else {
            int64_t p = static_cast<int64_t>(mid_price_) + tick_offset * static_cast<int64_t>(tick_size_);
            new_price = static_cast<Price>(std::max(p, static_cast<int64_t>(tick_size_)));
        }

        return GeneratedAction{ ActionType::REPLACE, id, 0, 0, Side::BUY, new_qty, new_price };
    }
}

std::vector<GeneratedAction> OrderGenerator::generate_batch(size_t n) {
    std::vector<GeneratedAction> actions;
    actions.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        actions.push_back(next());
    }
    return actions;
}

} // namespace MySTLOBs