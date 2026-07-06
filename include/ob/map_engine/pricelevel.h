#ifndef MY_ORDERBOOK_PRICELEVEL_H
#define MY_ORDERBOOK_PRICELEVEL_H

#include "ob/map_engine/types.h"
#include <cassert>
#include <cstdint>
#include <list>

namespace map_engine {

class PriceLevel{

public:

    struct FillResult{
        OrderId order_id;
        Quantity filled;
        bool fully_filled;
        Price fill_price;
    };

    explicit PriceLevel(Price p)
        : price_(p), total_volume_(0) {}

    std::list<Order>::iterator add_order(const Order& order){
        total_volume_ += order.remaining_quantity;
        return orders_.insert(orders_.end(), order);
    }

    void remove_order(std::list<Order>::iterator it){
        total_volume_ -= it->remaining_quantity;
        orders_.erase(it);
    }

    FillResult fill_front(Quantity available) {
        assert(!orders_.empty() && "fill_front called on empty PriceLevel");

        auto it = orders_.begin();
        if (available >= it->remaining_quantity) {
            FillResult result{ it->order_id, it->remaining_quantity, true, price_};
            total_volume_ -= it->remaining_quantity;
            orders_.erase(it);
            return result;
        } else {
            total_volume_          -= available;
            it->remaining_quantity -= available;
            return { it->order_id, available, false, price_ };
        }
    }

    bool     empty() const { return orders_.empty(); }
    uint64_t total_volume() const { return total_volume_; }
    Price    price() const { return price_; }

private:
    Price price_;
    uint64_t total_volume_;
    std::list<Order> orders_; 

};

} // namespace map_engine

#endif