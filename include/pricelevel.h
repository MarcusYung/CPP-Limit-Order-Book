#ifndef MY_ORDERBOOK_PRICELEVEL_H
#define MY_ORDERBOOK_PRICELEVEL_H

#include "types.h"
#include <list>
#include <cassert>

namespace MySTLOB {

class PriceLevel{

public:

    struct FillResult{
        OrderId order_id;
        Quantity filled;
        bool fully_filled;
    };

    explicit PriceLevel(Price p): price_(p), total_volume_(0) {}

    /*
        @Brief Appends an order to end of the price level list
            @param order: The incoming order to rest at this price level.
        @return std::list<Order>::iterator Iterator to the inserted order,
        required by the OrderMap for O(1) cancellations.
    */
    std::list<Order>::iterator add_order(const Order& order){
        total_volume_ += order.remaining_quantity;
        return orders_.insert(orders_.end(), order);
    }

    /*
        @Brief Remove an order from the price level list
            @param it: an iterator to direct access the order
        To achieve O(1) cancellations
    */
    void remove_order(std::list<Order>::iterator it){
        total_volume_ -= it->remaining_quantity;
        orders_.erase(it);
    }

    /*
        @Brief Fills the front order with up to 'available' quantity
        Handles both full and partial fill logic internally, so the caller no
        longer needs to reach into the list via front_it().    
    */
    FillResult fill_front(Quantity available) {
        assert(!orders_.empty() && "fill_front called on empty PriceLevel");
        auto it = orders_.begin();
        if (available >= it->remaining_quantity) {
            FillResult result{ it->order_id, it->remaining_quantity, true };
            total_volume_ -= it->remaining_quantity;
            orders_.erase(it);
            return result;
        } else {
            total_volume_ -= available;
            it->remaining_quantity -= available;
            return { it->order_id, available, false };
        }
    }

    bool    empty() const { return orders_.empty(); }
    uint64_t total_volume() const { return total_volume_; }
    Price   price() const { return price_; }

private:
    Price price_;
    uint64_t total_volume_; // Cached total quantities for O(1) volume lookup
    std::list<Order> orders_; // Doubly-Linked list for Time-Priority and FIFO

};

} // namespace MySTLOB

#endif