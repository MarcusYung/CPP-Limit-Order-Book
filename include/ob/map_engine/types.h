// include/ob/map_engine/types.h
#ifndef OB_MAP_ENGINE_TYPES_H
#define OB_MAP_ENGINE_TYPES_H

#include "ob/common.h"

namespace map_engine {

using ob::Price;  
using ob::Quantity;  
using ob::OrderId;
using ob::TradeId; 
using ob::Timestamp; 
using ob::now_ns;
using ob::Side;

struct Order {
    OrderId  order_id;
    Quantity initial_quantity;
    Quantity remaining_quantity;
    Price    price;
    Side     side;
};

struct Trade {
    TradeId     trade_id;
    OrderId     aggressive_order_id;
    OrderId     passive_order_id;
    Price       price;
    Quantity    quantity;
    Side        aggressor_side;
    Timestamp   timestamp_ns;
};

} // namespace map_engine
#endif