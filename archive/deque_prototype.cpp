#include <iostream>
#include <stdexcept>
#include <vector>
#include <cstdint>
#include <cassert>
#include <bitset>
#include <list>
#include <deque>
#include <algorithm>
#include <array>
#include <optional>

const int MIN_PRICE = 100;
const int MAX_PRICE = 200;
const int TICK_SIZE = 1;
const int NUM_SLOTS = (MAX_PRICE - MIN_PRICE) / TICK_SIZE;
const int NUM_WORDS = (NUM_SLOTS + 63) / 64;

int price_to_index(int price) {
    if (price < MIN_PRICE || price >= MAX_PRICE)
        throw std::out_of_range("price out of range");
    return (price - MIN_PRICE) / TICK_SIZE;
}

int index_to_price(int index) {
    if (index < 0 || index >= NUM_SLOTS)
        throw std::out_of_range("index out of range");
    return MIN_PRICE + index * TICK_SIZE;
}

uint64_t  set_bit(uint64_t bitmap, int slot_index) {
    return bitmap | (1ULL << slot_index);
}

uint64_t  clear_bit(uint64_t bitmap, int slot_index){
    return bitmap & ~(1ULL << slot_index);
}

bool check_bit(uint64_t bitmap, int slot_index){
    return (bitmap >> slot_index) & 1ULL;
}

int find_lowest_set_bit(uint64_t bitmap){
    return __builtin_ctzll(bitmap);
}

int find_highest_set_bit(uint64_t bitmap){
    return 63 - __builtin_clzll(bitmap);
}

// Word is the fuxed-sized unit of data that CPU can process in one go
void l2_set(std::array<uint64_t, NUM_WORDS>& l2_bitmap, uint64_t& l1_bitmap, int slot_index){
    int word_idx = slot_index / 64;
    int bit_pos = slot_index % 64;
    l2_bitmap[word_idx] |= (1ULL << bit_pos);
    l1_bitmap = set_bit(l1_bitmap, word_idx);
}

void l2_clear(std::array<uint64_t, NUM_WORDS>& l2_bitmap, uint64_t& l1_bitmap, int slot_index){
    int word_idx = slot_index / 64;
    int bit_pos = slot_index % 64;
    l2_bitmap[word_idx] &= ~(1ULL << bit_pos);
    if (l2_bitmap[word_idx] == 0)
        l1_bitmap = clear_bit(l1_bitmap, word_idx);
}

// The Shift-Down Method
bool l2_check(const std::array<uint64_t, NUM_WORDS>& bitmap, int slot_index){
    int word_idx = slot_index / 64;
    int bit_pos  = slot_index % 64;
    return (bitmap[word_idx] >> bit_pos) & 1ULL;
}

int find_best_low(const std::array<uint64_t, NUM_WORDS>& l2_bitmap, uint64_t l1_bitmap){
    if (l1_bitmap == 0)
        throw std::runtime_error("price not found");

    int l1_idx = find_lowest_set_bit(l1_bitmap);
    int l2_idx = find_lowest_set_bit(l2_bitmap[l1_idx]);

    return l1_idx * 64 + l2_idx;
}

int find_best_high(const std::array<uint64_t, NUM_WORDS>& l2_bitmap, uint64_t l1_bitmap){
    if (l1_bitmap == 0)
        throw std::runtime_error("price not found");
    int l1_idx = find_highest_set_bit(l1_bitmap);
    int l2_idx = find_highest_set_bit(l2_bitmap[l1_idx]);
    return l1_idx * 64 + l2_idx;
}

enum class Side { Bid, Ask };

struct Order {
    uint64_t id;
    Side     side;
    int      price;
    int      qty;
};

struct Trade {
    uint64_t maker_id;
    uint64_t taker_id;
    int      price;
    int      qty;
};

struct PriceLevel {
    int total_qty = 0;
    std::deque<Order> orders;  // FIFO

    void enqueue(const Order& order) {
        orders.push_back(order);
        total_qty += order.qty;
    }

    Order dequeue() {
        Order order = orders.front();
        orders.pop_front();
        total_qty -= order.qty;
        return order;
    }

    bool cancel(uint64_t order_id) {
        auto it = std::find_if( orders.begin(), orders.end(),
                             [order_id](const Order& order) { return order.id == order_id; });
                                
        if (it == orders.end()) return false;
        total_qty -= it->qty;
        orders.erase(it);
        return true;
    }

    Order& front() { return orders.front(); }
    bool is_empty() const { return orders.empty(); }

};

struct PriceLadder {
    uint64_t l1_bids = 0;
    uint64_t l1_asks = 0;
    std::array<uint64_t, NUM_WORDS> l2_bids;
    std::array<uint64_t, NUM_WORDS> l2_asks;
    std::array<PriceLevel, NUM_SLOTS> bid_levels;
    std::array<PriceLevel, NUM_SLOTS> ask_levels;

    void add_order(const Order& order) {
        int slot_index = price_to_index(order.price);

        if(order.side == Side::Bid){
            l2_set(l2_bids, l1_bids, slot_index);
            bid_levels[slot_index].enqueue(order);
        }

        if(order.side == Side::Ask){
            l2_set(l2_asks, l1_asks, slot_index);
            ask_levels[slot_index].enqueue(order);
        }
    }

    void cancel_order(const Order& order) {
        int slot_index = price_to_index(order.price);
        
        if(order.side == Side::Bid){
            bid_levels[slot_index].cancel(order.id);
            if (bid_levels[slot_index].is_empty()){
                l2_clear(l2_bids, l1_bids, slot_index);
            }
        }

        if(order.side == Side::Ask) {
            ask_levels[slot_index].cancel(order.id);
            if (ask_levels[slot_index].is_empty()){
                l2_clear(l2_asks, l1_asks, slot_index);
            }
        }
    }

    std::optional<int> best_ask() const {
        if (l1_asks == 0) return std::nullopt;
        return index_to_price(find_best_low(l2_asks, l1_asks));
    }

    std::optional<int> best_bid() const { 
        if (l1_bids == 0) return std::nullopt;
        return index_to_price(find_best_high(l2_bids, l1_bids));
    }

    std::vector<Trade> submit(Order incoming) {
        std::vector<Trade> executed;

        if(incoming.side == Side::Bid) {
           while (incoming.qty > 0){
                auto best = best_ask();
                if (!best) break;
                if (incoming.price < *best) break;

                int slot = price_to_index(*best);
                Order& maker = ask_levels[slot].front();

                int trade_qty = std::min(maker.qty, incoming.qty);
                executed.push_back(Trade{maker.id, incoming.id, maker.price, trade_qty});

                if (trade_qty == maker.qty) {
                    ask_levels[slot].dequeue();
                    if (ask_levels[slot].is_empty()) {
                        l2_clear(l2_asks, l1_asks, slot);
                    }
                } else {
                    maker.qty                   -= trade_qty;
                    ask_levels[slot].total_qty  -= trade_qty;
                }
                incoming.qty -= trade_qty;
            }
        }

        if(incoming.side == Side::Ask) {
            while (incoming.qty > 0){
                auto best = best_bid();
                if(!best) break;
                if(incoming.price > *best) break;

                int slot = price_to_index(*best);
                Order& maker = bid_levels[slot].front();

                int trade_qty = std::min(maker.qty, incoming.qty);
                executed.push_back(Trade{maker.id, incoming.id, maker.price, trade_qty});

                if (trade_qty == maker.qty){
                    bid_levels[slot].dequeue();
                    if(bid_levels[slot].is_empty()) {
                        l2_clear(l2_bids, l1_bids, slot);
                    }
                } else {
                    maker.qty                   -= trade_qty;
                    bid_levels[slot].total_qty  -= trade_qty;
                }
                incoming.qty -= trade_qty;
            }
        }

        if (incoming.qty > 0) {
            add_order(incoming);
        }

        return executed;
    }
};

void run_tests() {

    // index arithmetic
    assert(price_to_index(MIN_PRICE) == 0);
    assert(price_to_index(MAX_PRICE - TICK_SIZE) == NUM_SLOTS - 1);
    assert(index_to_price(0) == MIN_PRICE);

    bool threw = false;
    try { price_to_index(MAX_PRICE); }
    catch (const std::out_of_range&) { threw = true; }
    assert(threw);

    // single-word bit ops
    uint64_t w = 0;
    w = set_bit(w, 0);
    w = set_bit(w, 63);
    assert(check_bit(w, 0) && check_bit(w, 63) && !check_bit(w, 1));
    assert(find_lowest_set_bit(w) == 0);
    assert(find_highest_set_bit(w) == 63);
    w = clear_bit(w, 0);
    assert(find_lowest_set_bit(w) == 63);

    // L1 + L2 bitmap
    std::array<uint64_t, NUM_WORDS> l2{};
    uint64_t l1 = 0;

    threw = false;
    try { find_best_low(l2, l1); }
    catch (const std::runtime_error&) { threw = true; }
    assert(threw);

    l2_set(l2, l1, 0);
    l2_set(l2, l1, 63);
    l2_set(l2, l1, 64);
    l2_set(l2, l1, NUM_SLOTS - 1);

    for (int i = 0; i < NUM_WORDS; i++)
        assert(((l1 >> i) & 1ULL) == (l2[i] != 0));

    assert(find_best_low(l2, l1) == 0);
    assert(find_best_high(l2, l1) == NUM_SLOTS - 1);

    l2_clear(l2, l1, 0);
    assert(check_bit(l1, 0));
    assert(find_best_low(l2, l1) == 63);

    l2_clear(l2, l1, 63);
    assert(!check_bit(l1, 0));
    assert(check_bit(l1, 1));

    l2_clear(l2, l1, 64);
    l2_clear(l2, l1, NUM_SLOTS - 1);
    assert(l1 == 0);

    // Price Level test
    PriceLevel lv1;

    assert(lv1.is_empty());
    assert(lv1.total_qty == 0);

    lv1.enqueue({1, Side::Bid, 100, 10});
    lv1.enqueue({2, Side::Bid, 100, 5});
    lv1.enqueue({3, Side::Bid, 100,  7});
    assert(!lv1.is_empty());
    assert(lv1.total_qty == 22);

    Order first = lv1.dequeue();
    assert(first.id == 1);
    assert(lv1.total_qty == 12);

    assert(lv1.cancel(3) == true);   
    assert(lv1.cancel(99) == false);
    assert(lv1.total_qty == 5);

    Order last = lv1.dequeue();
    assert(last.id == 2);
    assert(lv1.is_empty());
    assert(lv1.total_qty == 0);


    // 6. PriceLadder construction

    PriceLadder ladder;

    assert(ladder.l1_bids == 0 && ladder.l1_asks == 0);
    assert(ladder.l2_bids.size() == NUM_WORDS);
    assert(ladder.bid_levels.size() == NUM_SLOTS);
    assert(ladder.bid_levels[0].is_empty());
    assert(ladder.ask_levels[NUM_SLOTS - 1].is_empty());

    // 7. add_order / cancel_order
    PriceLadder lad;

    lad.add_order({10, Side::Bid, 150, 5});
    lad.add_order({11, Side::Bid, 150, 3});
    lad.add_order({12, Side::Ask, 160, 7});

    int bid_slot = price_to_index(150);
    int ask_slot = price_to_index(160);

    assert(l2_check(lad.l2_bids, bid_slot));
    assert(l2_check(lad.l2_asks, ask_slot));
    assert(lad.bid_levels[bid_slot].total_qty == 8);
    assert(lad.ask_levels[ask_slot].total_qty == 7);

    lad.cancel_order({10, Side::Bid, 150, 5});
    assert(l2_check(lad.l2_bids, bid_slot));
    assert(lad.bid_levels[bid_slot].total_qty == 3);

    lad.cancel_order({11, Side::Bid, 150, 3});
    assert(!l2_check(lad.l2_bids, bid_slot));
    assert(lad.bid_levels[bid_slot].is_empty());

    // 8. BEST BID AND BEST ASK
    PriceLadder b;
    assert(!b.best_bid().has_value());
    assert(!b.best_ask().has_value());

    b.add_order({1, Side::Bid, 150, 1});
    b.add_order({2, Side::Ask, 155, 1});
    assert(b.best_bid() == 150);
    assert(b.best_ask() == 155);

    b.cancel_order({1, Side::Bid, 150, 1});
    assert(!b.best_bid().has_value());
    assert(b.best_ask() == 155);

    // Final: submit / matching

    // no match -> rests fully
    {
        PriceLadder p;
        auto trades = p.submit({100, Side::Bid, 150 ,5});
        assert(trades.empty());
        assert(p.best_bid() == 150);
        assert(p.bid_levels[price_to_index(150)].total_qty == 5);
    }

    // does not cross -> rests
    {
        PriceLadder p;
        p.add_order({1, Side::Ask, 160, 5});
        auto trades = p.submit({2, Side::Bid, 155, 3});
        assert(trades.empty());
        assert(p.best_bid() == 155);
        assert(p.best_ask() == 160);
    }

    // exact full fill order, taker == maker
    {
        PriceLadder p;
        p.add_order({1, Side::Ask, 160, 5});
        auto trades = p.submit({2, Side::Bid, 160, 5});
        assert(trades.size() == 1);
        assert(trades[0].maker_id == 1);
        assert(trades[0].taker_id == 2);
        assert(trades[0].price == 160);
        assert(trades[0].qty == 5);
        assert(!p.best_ask().has_value());
        assert(!p.best_bid().has_value());
        assert(p.ask_levels[price_to_index(160)].is_empty());
        assert(p.ask_levels[price_to_index(160)].total_qty == 0);
    }

    // walk multiple makers at one level (full fill each)
    {
        PriceLadder p;
        p.add_order({1, Side::Ask, 160, 4});
        p.add_order({2, Side::Ask, 160, 6});
        auto trades = p.submit({3, Side::Bid, 160, 10});
        assert(trades.size() == 2);
        assert(trades[0].maker_id == 1 && trades[0].qty == 4);  // FIFO
        assert(trades[1].maker_id == 2 && trades[1].qty == 6);
        assert(!p.best_ask().has_value());
    }

    // walk multiple price levels
    {
        PriceLadder p;
        p.add_order({1, Side::Ask, 160, 3});
        p.add_order({2, Side::Ask, 161, 4});
        p.add_order({3, Side::Ask, 162, 5});
        auto trades = p.submit({4, Side::Bid, 162, 12});
        assert(trades.size() == 3);
        assert(trades[0].price == 160 && trades[0].qty == 3);
        assert(trades[1].price == 161 && trades[1].qty == 4);
        assert(trades[2].price == 162 && trades[2].qty == 5);
        assert(!p.best_ask().has_value());
    }
    std::cout << "all tests passed\n";
}

int main() {
    run_tests();
}