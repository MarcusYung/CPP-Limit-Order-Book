#include "orderbook.h"
#include <stdexcept>

namespace MySTLOB {

void OrderBook::add_order(OrderId order_id, Quantity initial_quantity, Price price, Side side){

    if(hasOrder(order_id)) throw std::runtime_error("duplicate order id");

    Quantity remaining = initial_quantity;
    match_orders(side, price, remaining);

    if(remaining > 0){
        Order order{order_id, initial_quantity, remaining, price, side};
        PriceLevel* level_ptr = nullptr;
        std::list<Order>::iterator it;

        if(side == Side::BUY){
            auto& level = bids_.try_emplace(price, price).first->second;
            it = level.add_order(order);
            level_ptr = &level;
        } else {
            auto& level = asks_.try_emplace(price, price).first->second;
            it = level.add_order(order);
            level_ptr = &level;
        }

        order_map_[order_id] = OrderLocation{level_ptr, it};
    }
}

void OrderBook::match_orders(Side side, Price price, Quantity& remaining_quantity){

    if(side == Side::BUY){

        auto it = asks_.begin();
        while(remaining_quantity > 0 && it != asks_.end() && it->first <= price){
            auto& price_level = it->second;
            while(remaining_quantity > 0 && !price_level.empty()){
                auto result = price_level.fill_front(remaining_quantity);
                remaining_quantity -= result.filled;
                if (result.fully_filled)
                    order_map_.erase(result.order_id);
            }
            if(price_level.empty()) it = asks_.erase(it);
            else ++it;
        }

    }

    if(side == Side::SELL){
        auto it = bids_.begin();
        while (remaining_quantity > 0 && it != bids_.end() && it->first >= price) {
            auto& price_level = it->second;
            while (remaining_quantity > 0 && !price_level.empty()){
                    auto result = price_level.fill_front(remaining_quantity);
                    remaining_quantity -= result.filled;
                    if (result.fully_filled)
                        order_map_.erase(result.order_id);
            }
            if(price_level.empty()) it = bids_.erase(it);
            else ++it;
        }
    }
}

bool OrderBook::cancel_order(OrderId order_id){

    auto map_it = order_map_.find(order_id);
    if(map_it == order_map_.end()) return false;

    OrderLocation&  loc = map_it->second;
    Side            side = loc.it->side;
    Price           price = loc.it->price;

    loc.price_level->remove_order(loc.it);

    if(loc.price_level->empty())
        remove_empty_price_level(side, price);

    order_map_.erase(map_it);
    return true;
}

void OrderBook::remove_empty_price_level(Side side, Price price){
    if (side == Side::BUY)  bids_.erase(price);
    else                    asks_.erase(price);
}

std::optional<Price> OrderBook::bestBid() const {
    if(bids_.empty()) return std::nullopt;
    return bids_.begin()->first;
}

std::optional<Price> OrderBook::bestAsk() const {
    if(asks_.empty()) return std::nullopt;
    return asks_.begin()->first;
}

bool OrderBook::hasOrder(OrderId order_id) const{
    return order_map_.find(order_id) != order_map_.end();
}

void OrderBook::printBook() const{

    std::cout   << std::left << std::setw(15) << "Side"
                << std::right << std::setw(10) << "Price"
                << std::right << std::setw(10) << "Quantity" << "\n";
    std::cout << std::setfill('-') << std::setw(35) << "-" << "\n";
    std::cout << std::setfill(' ');

    for(auto it = asks_.rbegin(); it != asks_.rend(); ++it) {
        std::cout   << std::left << std::setw(15) << "SELL"
                    << std::right << std::setw(10) << it->first
                    << std::right << std::setw(10) << it->second.total_volume() << "\n";
    }

    std::cout << std::setfill('-') << std::setw(35) << "-" << "\n";

    if (bestBid() && bestAsk()) {
        int64_t spread = static_cast<int64_t>(*bestAsk()) - static_cast<int64_t>(*bestBid());
        std::cout << " spread: " << spread << "\n";
        std::cout << std::setfill('-') << std::setw(35) << "-" << "\n";
    }

    std::cout << std::setfill(' ');

    for(const auto& [price, price_level]: bids_) {
        std::cout   << std::left << std::setw(15) << "BUY"
                    << std::right << std::setw(10) << price
                    << std::right << std::setw(10) << price_level.total_volume() << "\n";
    }

}

} // namespace MySTLOB