#ifndef OB_ADAPTERS_H
#define OB_ADAPTERS_H

#include "ob/common.h"
#include "ob/map_engine/orderbook.h"
#include <vector>

namespace ob {

inline ob::OrderStatus feed(map_engine::OrderBook& book, const ob::Event& event, std::vector<ob::TradeRec>& out) {

    if(event.type == ob::Event::Type::Cancel){
        book.cancel_order(event.order_id);
        return ob::OrderStatus::Success;
    } 

    if (event.order_id == 0) [[unlikely]] {
        return ob::OrderStatus::InvalidOrderId;
    }

    if (event.qty == 0) [[unlikely]] {
        return ob::OrderStatus::InvalidQuantity;
    }

    if (book.has_order(event.order_id)) [[unlikely]] {
        return ob::OrderStatus::DuplicateId;
    }

    auto executed_trades = book.submit(event.order_id, event.side, event.price, event.qty);

    for (const auto& raw_trade : executed_trades) {
        out.push_back(ob::TradeRec{
            raw_trade.passive_order_id,
            raw_trade.aggressive_order_id,
            raw_trade.price,
            raw_trade.quantity
        });
    }

    return ob::OrderStatus::Success;
}

} // namespace ob

#endif