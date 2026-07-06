#include "ob/bitmap_engine/ladder.h"
#include <stdexcept>
#include <algorithm>
#include <bit>
#include <cassert>

namespace ob::bitmap_engine {

BitmapOrderBook::BitmapOrderBook(const LadderConfig& cfg) : config(cfg), pool(kOrderMapMaxLoad){

    if (config.tick_size == 0) [[unlikely]] {
        throw std::invalid_argument("tick_size must be > 0");
    }

    if (config.max_price < config.min_price) [[unlikely]] {
        throw std::invalid_argument("max_price must be >= min_price");
    }

    const std::uint64_t span = static_cast<std::uint64_t>(config.max_price) - static_cast<std::uint64_t>(config.min_price);

    size_t num_slots = static_cast<std::size_t>(span / config.tick_size) + 1;

    bid_levels.resize(num_slots);
    ask_levels.resize(num_slots);

    size_t L3_size = (num_slots + 63) / 64;
    size_t L2_size = (L3_size + 63) / 64;
    size_t L1_size = (L2_size + 63) / 64;

    l3_asks.resize(L3_size);
    l3_bids.resize(L3_size);
    
    l2_asks.resize(L2_size);
    l2_bids.resize(L2_size);

    l1_asks.resize(L1_size);
    l1_bids.resize(L1_size);
}

void BitmapOrderBook::set_bit_hierarchical(ob::Side side, PriceIndex slot_index) {
    const PriceIndex word_idx_l3 = slot_index >> kWordShift;
    const PriceIndex bit_pos_l3  = slot_index & (kWordBits - 1);

    const PriceIndex word_idx_l2 = word_idx_l3 >> kWordShift;
    const PriceIndex bit_pos_l2  = word_idx_l3 & (kWordBits - 1);

    const PriceIndex word_idx_l1 = word_idx_l2 >> kWordShift;
    const PriceIndex bit_pos_l1  = word_idx_l2 & (kWordBits - 1);

    if (side == ob::Side::Bid) {
        l3_bids[word_idx_l3] |= (1ULL << bit_pos_l3);
        l2_bids[word_idx_l2] |= (1ULL << bit_pos_l2);
        l1_bids[word_idx_l1] |= (1ULL << bit_pos_l1);
    } else {
        l3_asks[word_idx_l3] |= (1ULL << bit_pos_l3);
        l2_asks[word_idx_l2] |= (1ULL << bit_pos_l2);
        l1_asks[word_idx_l1] |= (1ULL << bit_pos_l1);
    }
}

void BitmapOrderBook::clear_bit_hierarchical(ob::Side side, PriceIndex slot_index) {

    const PriceIndex word_idx_l3 = slot_index >> kWordShift;
    const PriceIndex bit_pos_l3  = slot_index & (kWordBits - 1);

    if (side == ob::Side::Bid) {
        auto& w3 = l3_bids[word_idx_l3];
        if ((w3 &= ~(1ULL << bit_pos_l3)) == 0) {
            const PriceIndex word_idx_l2 = word_idx_l3 >> kWordShift;
            const PriceIndex bit_pos_l2  = word_idx_l3 & (kWordBits - 1);

            auto& w2 = l2_bids[word_idx_l2];
            if ((w2 &= ~(1ULL << bit_pos_l2)) == 0){
                const PriceIndex word_idx_l1 = word_idx_l2 >> kWordShift;
                const PriceIndex bit_pos_l1  = word_idx_l2 & (kWordBits - 1);

                l1_bids[word_idx_l1] &= ~(1ULL << bit_pos_l1);
            }
        }

    } else {
        auto& w3 = l3_asks[word_idx_l3];
        if ((w3 &= ~(1ULL << bit_pos_l3)) == 0) {
            const PriceIndex word_idx_l2 = word_idx_l3 >> kWordShift;
            const PriceIndex bit_pos_l2  = word_idx_l3 & (kWordBits - 1);

            auto& w2 = l2_asks[word_idx_l2];
            if ((w2 &= ~(1ULL << bit_pos_l2)) == 0){
                const PriceIndex word_idx_l1 = word_idx_l2 >> kWordShift;
                const PriceIndex bit_pos_l1  = word_idx_l2 & (kWordBits - 1);

                l1_asks[word_idx_l1] &= ~(1ULL << bit_pos_l1);
            }
        }
    }
}

std::optional<PriceIndex> BitmapOrderBook::find_best_bid_idx() const {

    for(int64_t i = static_cast<int64_t>(l1_bids.size()) - 1; i >=0; --i){
        if (l1_bids[i] != 0) {
            PriceIndex bit_pos_l1 = 63 - std::countl_zero(l1_bids[i]);
            PriceIndex idx_l2     = static_cast<PriceIndex>(i) * kWordBits + bit_pos_l1;

            PriceIndex bit_pos_l2 = 63 - std::countl_zero(l2_bids[idx_l2]);
            PriceIndex idx_l3     = idx_l2 * kWordBits + bit_pos_l2;

            PriceIndex bit_pos_l3 = 63 - std::countl_zero(l3_bids[idx_l3]);
            return idx_l3 * kWordBits + bit_pos_l3;
        }
    }
    return std::nullopt;
}

std::optional<PriceIndex> BitmapOrderBook::find_best_ask_idx() const {
    for (std::size_t i = 0; i < l1_asks.size(); ++i) {
        if (l1_asks[i] != 0) {
            PriceIndex bit_pos_l1 =
                static_cast<PriceIndex>(std::countr_zero(l1_asks[i]));

            PriceIndex idx_l2 =
                static_cast<PriceIndex>(i) * kWordBits + bit_pos_l1;

            if (idx_l2 >= l2_asks.size() || l2_asks[idx_l2] == 0) {
                return std::nullopt;
            }

            PriceIndex bit_pos_l2 =
                static_cast<PriceIndex>(std::countr_zero(l2_asks[idx_l2]));

            PriceIndex idx_l3 =
                idx_l2 * kWordBits + bit_pos_l2;

            if (idx_l3 >= l3_asks.size() || l3_asks[idx_l3] == 0) {
                return std::nullopt;
            }

            PriceIndex bit_pos_l3 =
                static_cast<PriceIndex>(std::countr_zero(l3_asks[idx_l3]));

            PriceIndex result =
                idx_l3 * kWordBits + bit_pos_l3;

            if (result >= ask_levels.size()) {
                return std::nullopt;
            }

            return result;
        }
    }

    return std::nullopt;
}

std::optional<ob::Price> BitmapOrderBook::best_ask() const {
    auto idx = find_best_ask_idx();
    if (idx) return index_to_price(*idx);
    return std::nullopt;
}

std::optional<ob::Price> BitmapOrderBook::best_bid() const {
    auto idx = find_best_bid_idx();
    if (idx) return index_to_price(*idx);
    return std::nullopt;
}

PriceIndex BitmapOrderBook::price_to_index(ob::Price price) const {
    return static_cast<PriceIndex>((price - config.min_price) / config.tick_size);
}

ob::Price BitmapOrderBook::index_to_price(PriceIndex index) const {
    assert(index < bid_levels.size() && "index out of range");
    return static_cast<ob::Price>(config.min_price + static_cast<ob::Price>(index) * config.tick_size);
}

OrderPool::OrderPool(size_t capacity) {
    slots.resize(capacity);  // all slots constructed
    for (OrderIndex i = 0; i < capacity - 1; i++)
        slots[i].next = i + 1;
    slots[capacity - 1].next = kInvalidOrderIndex;
    free_head = 0;
}

OrderIndex OrderPool::allocate(const Order& data) {
    if(free_head == kInvalidOrderIndex) {
        throw std::runtime_error("pool exhausted");
    }
    OrderIndex idx = free_head;
    free_head = slots[idx].next;
    slots[idx] = data;
    return idx;
}

// not to erase the data, next time alloacate() is called, it grab the slot and overwrite it with new order data
void OrderPool::deallocate(OrderIndex idx) {
    slots[idx].prev  = kInvalidOrderIndex;
    slots[idx].next = free_head;
    free_head = idx;
}

bool OrderPool::is_full() const {
    return free_head == kInvalidOrderIndex;
}
void PriceLevel::push_back(OrderIndex idx, OrderPool& pool) {
    pool.slots[idx].next = kInvalidOrderIndex;

    if (head == kInvalidOrderIndex) {
        pool.slots[idx].prev = kInvalidOrderIndex;
        head = idx;
        tail = idx;
    } else {
        pool.slots[idx].prev = tail;
        pool.slots[tail].next = idx;
        tail = idx;
    }
    total_qty += pool.slots[idx].qty;
}

OrderIndex PriceLevel::pop_front(OrderPool& pool) {
    assert(head != kInvalidOrderIndex && "no order");

    OrderIndex front_idx = head;
    OrderIndex next_idx  = pool.slots[front_idx].next;
    Quantity removed_qty = pool.slots[front_idx].qty;

    if (head == tail){
        head = kInvalidOrderIndex;
        tail = kInvalidOrderIndex;
    } else {
        head = next_idx;
        pool.slots[head].prev = kInvalidOrderIndex;
    }

    pool.slots[front_idx].prev = kInvalidOrderIndex;
    pool.slots[front_idx].next = kInvalidOrderIndex;

    total_qty -= removed_qty;
    return front_idx;
}

void PriceLevel::unlink(OrderIndex idx, OrderPool& pool) {
    assert(head != kInvalidOrderIndex && "no order");

    OrderIndex prev_idx = pool.slots[idx].prev;
    OrderIndex next_idx = pool.slots[idx].next;
    Quantity removed_qty = pool.slots[idx].qty;

    if (prev_idx != kInvalidOrderIndex)
        pool.slots[prev_idx].next = next_idx;
    else
        head = next_idx;

    if (next_idx != kInvalidOrderIndex)
        pool.slots[next_idx].prev = prev_idx;
    else
        tail = prev_idx;

    pool.slots[idx].prev = kInvalidOrderIndex; 
    pool.slots[idx].next = kInvalidOrderIndex;

    total_qty -= removed_qty;
}

bool PriceLevel::is_empty() const {
    return head == kInvalidOrderIndex;
}

OrderIndex PriceLevel::front() const {
    return head;
}

void BitmapOrderBook::add_resting_order(ob::OrderId id, ob::Side side, ob::Price price, ob::Quantity qty) {
    if( qty == 0 ){
        return;
    }

    PriceIndex slot_index = price_to_index(price);

    Order order{id, side, price, qty, kInvalidOrderIndex, kInvalidOrderIndex};

    OrderIndex idx = pool.allocate(order);
    id_to_idx.insert(id, idx);

    if (side == Side::Bid) {
        bid_levels[slot_index].push_back(idx, pool);
        set_bit_hierarchical(ob::Side::Bid, slot_index);
    } else {
        ask_levels[slot_index].push_back(idx, pool);
        set_bit_hierarchical(ob::Side::Ask, slot_index);
    }
}

bool BitmapOrderBook::cancel_order(ob::OrderId id){
    auto idx = id_to_idx.find(id);
    if (idx == kInvalidOrderIndex) return false;

    ob::Price price = pool.slots[idx].price;
    ob::Side side   = pool.slots[idx].side;
    PriceIndex slot_index  = price_to_index(price);

    if(side == ob::Side::Bid){
        bid_levels[slot_index].unlink(idx, pool);
        if (bid_levels[slot_index].is_empty())
            clear_bit_hierarchical(ob::Side::Bid, slot_index);
    } else {
        ask_levels[slot_index].unlink(idx, pool);
        if (ask_levels[slot_index].is_empty())
            clear_bit_hierarchical(ob::Side::Ask, slot_index);
    }
    
    pool.deallocate(idx);
    id_to_idx.erase(id);

    return true;
}

std::vector<ob::TradeRec> BitmapOrderBook::add_order(
    ob::OrderId id,
    ob::Side side,
    ob::Price price,
    ob::Quantity qty
) {
    std::vector<ob::TradeRec> trades;
    const ob::OrderStatus status = submit(id, side, price, qty, trades);

    if (status != ob::OrderStatus::Success) {
        throw std::runtime_error("BitmapOrderBook::add_order rejected order");
    }

    return trades;
}

ob::OrderStatus BitmapOrderBook::submit(ob::OrderId id, ob::Side side, ob::Price price, ob::Quantity qty, std::vector<ob::TradeRec>& executed_trades) {

    if(qty == 0) [[unlikely]] {
        return ob::OrderStatus::InvalidQuantity;
    }

    if (id == 0) [[unlikely]] {
        return ob::OrderStatus::InvalidOrderId;
    }

    if(price < config.min_price || price > config.max_price || (price - config.min_price) % config.tick_size != 0) [[unlikely]] {
        return ob::OrderStatus::PriceOutOfBounds;
    }

    if(id_to_idx.find(id) != kInvalidOrderIndex) {
        return ob::OrderStatus::DuplicateId;
    }

    if (pool.is_full() || id_to_idx.is_full()) {
        return ob::OrderStatus::EngineFull;
    }

    if(side == ob::Side::Bid){
        while (qty > 0){
            auto best = best_ask();
            if(!best || price < *best) break;

            PriceIndex slot_index = price_to_index(*best);
            OrderIndex maker_idx = ask_levels[slot_index].front();
            Order& maker = pool.slots[maker_idx];

            if (maker.next != kInvalidOrderIndex) [[likely]] {
                __builtin_prefetch(&pool.slots[maker.next], 0, 1);
            }

            ob::Quantity trade_qty = std::min(maker.qty, qty);
            executed_trades.push_back(ob::TradeRec{maker.id, id, maker.price, trade_qty});

            if(trade_qty == maker.qty) [[likely]] {
                ask_levels[slot_index].pop_front(pool);
                id_to_idx.erase(maker.id);
                pool.deallocate(maker_idx);

                if (ask_levels[slot_index].is_empty()) [[unlikely]] {
                    clear_bit_hierarchical(ob::Side::Ask, slot_index);
                }
            } else {
                maker.qty                           -= trade_qty;
                ask_levels[slot_index].total_qty    -= trade_qty;
            }
            qty -= trade_qty;
        }
    } else {

        while (qty > 0){
            auto best = best_bid();
            if(!best) break;
            if(price > *best) break;

            PriceIndex slot_index  = price_to_index(*best);
            OrderIndex maker_idx   = bid_levels[slot_index].front();
            Order& maker           = pool.slots[maker_idx];

            if (maker.next != kInvalidOrderIndex) [[likely]] {
                __builtin_prefetch(&pool.slots[maker.next], 0, 1);
            }

            ob::Quantity trade_qty = std::min(maker.qty, qty);
            executed_trades.push_back(ob::TradeRec{maker.id, id, maker.price, trade_qty});

            if (trade_qty == maker.qty) [[likely]] {
                bid_levels[slot_index].pop_front(pool);
                id_to_idx.erase(maker.id);
                pool.deallocate(maker_idx);
                if(bid_levels[slot_index].is_empty()) {
                    clear_bit_hierarchical(ob::Side::Bid, slot_index);
                }
            } else {
                maker.qty                           -= trade_qty;
                bid_levels[slot_index].total_qty    -= trade_qty;
            }
            qty -= trade_qty;
        }
    }

    if (qty > 0) [[unlikely]] {
        add_resting_order(id, side, price, qty);
    }

    return ob::OrderStatus::Success;
}

} // namespace  bitmap_engine