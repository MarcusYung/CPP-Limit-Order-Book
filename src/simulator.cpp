#include "ob/simulator.h"
#include <iostream>
#include <random>

SimulatorResult generate_events(Workload profile, const SimulatorConfig& config) {
    uint64_t current_order_id = 1;
    std::mt19937 rng(config.random_seed);

    uint64_t total_levels = (config.max_price - config.min_price) / config.tick_size;
    std::uniform_int_distribution<int> price_dist(0, total_levels);
    std::uniform_int_distribution<int> quantity_dist(config.min_quantity, config.max_quantity);
    std::bernoulli_distribution side_dist(config.buy_prob);

    std::vector<ob::OrderId> live_orders;
    std::bernoulli_distribution do_cancel_dist(config.cancel_prob);

    SimulatorResult result;

    auto add_event = [&]() {
        ob::Quantity qty = quantity_dist(rng);
        ob::Price price = config.min_price + price_dist(rng) * config.tick_size;
        ob::Side side    = side_dist(rng) ? ob::Side::Bid : ob::Side::Ask;
        ob::Event event = {ob::Event::Type::New, current_order_id, price, qty, side};

        result.events.push_back(event);
        live_orders.push_back(current_order_id);
        result.num_add_events++;
        current_order_id++;
    };

    auto cancel_event = [&]() {
        std::uniform_int_distribution<size_t> index_dist(0, live_orders.size() - 1);
        uint64_t idx = index_dist(rng);
        ob::OrderId target_id = live_orders[idx];
        ob::Event event = {ob::Event::Type::Cancel, target_id, 0, 0, ob::Side::Bid};    // Price, qty, side are ignored by engines
        result.events.push_back(event);
        result.num_cancel_events++;
        live_orders[idx] = live_orders.back();
        live_orders.pop_back();
    };

    if (profile == Workload::AddOnly) {
        for(int i = 0; i < static_cast<int>(config.total_events); i++){
            add_event();
        }
    }

    if (profile == Workload::CancelHeavy) {
        for(int i = 0; i < static_cast<int>(config.total_events); i++){

            bool is_cancel;

            if(live_orders.empty()){
                add_event();
            } else {
                is_cancel = do_cancel_dist(rng);
                is_cancel ? cancel_event() : add_event();
            }
        }
    }

    if (profile == Workload::CrossingHeavy) {
        uint32_t lower_levels = config.mid_price - config.min_price;
        uint32_t upper_levels = config.max_price - config.mid_price;

        std::uniform_int_distribution<int> lower_price_dist(0, lower_levels - 1);
        std::uniform_int_distribution<int> upper_price_dist(1, upper_levels);

        for(int i = 0; i < static_cast<int>(config.total_events) / 2; i++) {
            ob::Quantity qty = quantity_dist(rng);
            ob::Side side    = side_dist(rng) ? ob::Side::Bid : ob::Side::Ask;
            ob::Price price;

            if (side == ob::Side::Bid){
                price = config.min_price + lower_price_dist(rng) * config.tick_size;
            } else {
                price = config.mid_price + upper_price_dist(rng) * config.tick_size;
            }

            ob::Event event = {ob::Event::Type::New, current_order_id, price, qty, side};
            result.events.push_back(event);
            live_orders.push_back(current_order_id);
            result.num_add_events++;
            current_order_id++;
        }

        for(int i = static_cast<int>(config.total_events) / 2; i < static_cast<int>(config.total_events); i++) {
            ob::Quantity qty = quantity_dist(rng);
            ob::Side side    = side_dist(rng) ? ob::Side::Bid : ob::Side::Ask;
            ob::Price price;

            if (side == ob::Side::Bid){
                price = config.mid_price + upper_price_dist(rng) * config.tick_size;
            } else {
                price = config.min_price + lower_price_dist(rng) * config.tick_size;
            }
            ob::Event event = {ob::Event::Type::New, current_order_id, price, qty, side};
            result.events.push_back(event);
            live_orders.push_back(current_order_id);
            result.num_add_events++;
            current_order_id++;
        }
    }

    return result;
}

// config.mid_price = config.min_price + ((config.max_price - config.min_price) / 2);