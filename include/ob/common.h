#ifndef OB_COMMON_H
#define OB_COMMON_H

#include <cstdint>
#include <chrono>

namespace ob {

using Price     = uint32_t;
using Quantity  = uint32_t;
using OrderId   = uint64_t;
using TradeId   = uint64_t;
using Timestamp = uint64_t;

enum class Side : uint8_t { Bid, Ask, Buy = Bid, Sell = Ask};

struct Event {
    enum class Type : uint8_t { New, Cancel } type;
    OrderId  order_id;
    Price    price;
    Quantity qty;
    Side     side;
};

struct TradeRec {
    OrderId  maker_id;   // passive 
    OrderId  taker_id;   // aggressive
    Price    price;
    Quantity qty;
};

enum class OrderStatus : uint8_t {
    Success = 0,
    DuplicateId,
    PriceOutOfBounds,
    EngineFull,
    InvalidQuantity,
    InvalidOrderId
};

inline bool operator==(const TradeRec& a, const TradeRec& b) {
    return a.maker_id == b.maker_id && a.taker_id == b.taker_id
        && a.price == b.price && a.qty == b.qty;
}

inline Timestamp now_ns() {
    using namespace std::chrono;
    return static_cast<Timestamp>(
        duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count());
}

} // namespace ob

#endif