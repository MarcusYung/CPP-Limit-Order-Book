#ifndef MY_ORDERBOOK_TYPES_H
#define MY_ORDERBOOK_TYPES_H

#include <cstdint>

namespace MySTLOB{

using Price = uint32_t;
using Quantity = uint32_t;
using OrderId = uint64_t;

enum class Side : uint8_t { 
    BUY, 
    SELL 
};

struct Order{
    OrderId order_id;           // 8 bytes
    Quantity initial_quantity;  // 4 bytes
    Quantity remaining_quantity;// 4 bytes
    Price price;                // 4 bytes
    Side side;                  // 1 byte
};

} // namespace MySTLOB

#endif