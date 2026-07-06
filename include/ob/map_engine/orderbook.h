// include/ob/map_engine/orderbook.h

#ifndef MY_ORDERBOOK_ORDERBOOK_H
#define MY_ORDERBOOK_ORDERBOOK_H

#include "ob/map_engine/types.h"      
#include "ob/map_engine/pricelevel.h" 

#include <functional>
#include <list>
#include <map>
#include <optional>
#include <unordered_map>
#include <vector>

namespace map_engine {

class OrderBook{

public:
    
    std::vector<Trade> submit(
        OrderId order_id, 
        Side side, 
        Price price, 
        Quantity initial_quantity
    );

    std::vector<Trade> add_order(
        OrderId order_id,
        Side side,
        Price price,
        Quantity initial_quantity
    ) {
        return submit(order_id, side, price, initial_quantity);
    }

    bool cancel_order(OrderId order_id);

    std::optional<Price> best_bid() const;
    std::optional<Price> best_ask() const;
    bool has_order(OrderId order_id) const;
    void print_book() const;

private:

    TradeId next_trade_id_ = 1;
    
    struct OrderLocation{
        PriceLevel*                 price_level;
        std::list<Order>::iterator  it;
    };

    void match_orders(
        OrderId aggressor_id,
        Side side, 
        Price price, 
        Quantity& remaining_quantity, 
        std::vector<Trade>& executed_trades
    );

    void remove_empty_price_level(Side side, Price price);

    std::map<Price, PriceLevel, std::greater<Price>> bids_; 
    std::map<Price, PriceLevel>                      asks_;
    std::unordered_map<OrderId, OrderLocation>       order_map_;
};

} // namespace map_engine

using MapEngine = map_engine::OrderBook;

#endif