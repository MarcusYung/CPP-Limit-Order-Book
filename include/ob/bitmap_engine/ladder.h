#pragma once

#include "ob/common.h"
#include "ob/bitmap_engine/types.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>
#include "ob/bitmap_engine/order_map.h"

namespace ob::bitmap_engine {

using ob::Price; 
using ob::Quantity; 
using ob::OrderId;
using ob::Side;           
using ob::TradeRec;   

struct Order{
    OrderId         id;
    Side            side;
    Price           price;
    Quantity        qty;
    OrderIndex      prev;
    OrderIndex      next;
};

struct OrderPool {
    std::vector<Order> slots;
    OrderIndex free_head;

    explicit OrderPool(size_t capacity);
    OrderIndex allocate(const Order& data);
    void deallocate(OrderIndex idx);
    bool is_full() const;

};

struct alignas(16) PriceLevel {
    OrderIndex head = kInvalidOrderIndex;
    OrderIndex tail = kInvalidOrderIndex;
    uint64_t total_qty = 0;

    void push_back(OrderIndex idx, OrderPool& pool);
    OrderIndex pop_front(OrderPool& pool);
    void unlink(OrderIndex  idx, OrderPool& pool);

    bool is_empty() const;
    OrderIndex  front() const;
};

class BitmapOrderBook {
public:

    explicit BitmapOrderBook(const LadderConfig& cfg = LadderConfig{});

    BitmapOrderBook(ob::Price min_price, ob::Price max_price): BitmapOrderBook(LadderConfig{min_price, max_price, 1}) {}

    std::optional<Price> best_ask() const;
    std::optional<Price> best_bid() const;

    std::vector<ob::TradeRec> add_order(OrderId  id, Side side, Price price, Quantity qty);
    bool cancel_order(OrderId  id);
    [[nodiscard]] ob::OrderStatus submit(OrderId id, Side side, Price price, Quantity qty, std::vector<ob::TradeRec>& executed_trades);

private:

    void set_bit_hierarchical(ob::Side side, PriceIndex slot_index);
    void clear_bit_hierarchical(ob::Side side, PriceIndex slot_index);
    void add_resting_order(OrderId id, Side side, Price price, Quantity qty);

    std::optional<PriceIndex> find_best_bid_idx() const;
    std::optional<PriceIndex> find_best_ask_idx() const;

    PriceIndex price_to_index(ob:: Price price) const;
    ob::Price index_to_price(PriceIndex index)  const;

    LadderConfig config;

    std::vector<BitmapWord> l1_bids, l1_asks;
    std::vector<BitmapWord> l2_bids, l2_asks;
    std::vector<BitmapWord> l3_bids, l3_asks;

    std::vector<PriceLevel> bid_levels;
    std::vector<PriceLevel> ask_levels;

    OrderPool pool;
    OrderMap  id_to_idx;

};

} // namespace ob::bitmap_engine

using BitmapEngine = ob::bitmap_engine::BitmapOrderBook;