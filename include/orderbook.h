#ifndef MY_ORDERBOOK_ORDERBOOK_H
#define MY_ORDERBOOK_ORDERBOOK_H

#include "types.h"
#include "pricelevel.h"
#include <map>
#include <list>
#include <unordered_map>
#include <optional>
#include <iostream>
#include <iomanip>

namespace MySTLOB {

class OrderBook{

public:
    
    void add_order(OrderId order_id, Quantity initial_quantity, Price price, Side side);
    bool cancel_order(OrderId order_id);

    std::optional<Price> bestBid() const;
    std::optional<Price> bestAsk() const;
    bool hasOrder(OrderId order_id) const;
    void printBook() const;

private:

    struct OrderLocation{
        PriceLevel*                 price_level;
        std::list<Order>::iterator  it;
    };

    void match_orders(Side side, Price price, Quantity& remaining_quantity);
    void remove_empty_price_level(Side side, Price price);

    std::map<Price, PriceLevel, std::greater<Price>> bids_; 
    std::map<Price, PriceLevel>                      asks_;
    std::unordered_map<OrderId, OrderLocation>       order_map_;
};

} // namespace MySTLOB

#endif
