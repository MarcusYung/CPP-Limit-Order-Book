#ifndef MY_ORDERBOOK_GENERATOR_H
#define MY_ORDERBOOK_GENERATOR_H

#include "types.h"
#include <vector>
#include <random>
#include <cstdint>

namespace MySTLOB { 

enum class ActionType : uint8_t {
    ADD,
    CANCEL,
    REPLACE
};

struct GeneratedAction {
    ActionType  type;
    OrderId     order_id;
    Quantity    quantity;
    Price       price;
    Side        side;
    Quantity    new_quantity;
    Price       new_price;
};

class OrderGenerator {

public:

    OrderGenerator(Price mid_price, Price tick_size, uint64_t seed);

    GeneratedAction next();

    std::vector<GeneratedAction> generate_batch(size_t n);

private:

    Price       mid_price_;
    Price       tick_size_;
    uint64_t    next_order_id_;
    std::vector<OrderId> live_orders_;  // track IDs that are likely still in book

    std::mt19937_64 rng_;
    std::normal_distribution<double> price_offset_dist_;
    std::uniform_int_distribution<uint32_t> quantity_dist_;
    std::uniform_real_distribution<double> action_dist_;
    std::uniform_real_distribution<double> side_dist_;
    std::normal_distribution<double> drift_dist_; 
};

} // namespace MYSTLOB

#endif