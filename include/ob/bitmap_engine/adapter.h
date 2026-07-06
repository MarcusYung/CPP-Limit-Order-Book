#ifndef OB_FEED_BITMAP_H
#define OB_FEED_BITMAP_H

#include <iostream>
#include "ob/common.h"
#include "ob/bitmap_engine/ladder.h"
#include <vector>

namespace ob {

inline ob::OrderStatus feed(bitmap_engine::BitmapOrderBook& book, const ob::Event& event, std::vector<ob::TradeRec>& out) {
    if (event.type == ob::Event::Type::Cancel) {
        book.cancel_order(event.order_id);
        return ob::OrderStatus::Success;
    }

    return book.submit(event.order_id, event.side, event.price, event.qty, out);
}

} // namespace ob

#endif