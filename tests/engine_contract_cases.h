#pragma once

#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace obtest {

using OrderId = std::uint64_t;
using Price = std::int64_t;
using Qty = std::int64_t;

enum class Side {
    Buy,
    Sell
};

struct Trade {
    Price price = 0;
    Qty qty = 0;
    std::optional<OrderId> maker_order_id = std::nullopt;
    std::optional<OrderId> taker_order_id = std::nullopt;

    Trade() = default;

    Trade(
        Price p,
        Qty q,
        std::optional<OrderId> maker = std::nullopt,
        std::optional<OrderId> taker = std::nullopt
    )
        : price(p),
          qty(q),
          maker_order_id(maker),
          taker_order_id(taker) {}
};

struct AddResult {
    bool accepted = true;
    std::vector<Trade> trades;
};

class TestFailure : public std::runtime_error {
public:
    explicit TestFailure(const std::string& message)
        : std::runtime_error(message) {}
};

template <typename T>
std::string show(const T& value) {
    std::ostringstream oss;
    oss << value;
    return oss.str();
}

inline std::string show(const bool& value) {
    return value ? "true" : "false";
}

template <typename T>
std::string show(const std::optional<T>& value) {
    if (!value.has_value()) {
        return "nullopt";
    }

    return "optional(" + show(*value) + ")";
}

inline std::string show(const Trade& trade) {
    std::ostringstream oss;
    oss << "{price=" << trade.price
        << ", qty=" << trade.qty
        << ", maker=" << show(trade.maker_order_id)
        << ", taker=" << show(trade.taker_order_id)
        << "}";
    return oss.str();
}

#define OBTEST_REQUIRE(expr)                                                         \
    do {                                                                            \
        if (!(expr)) {                                                              \
            std::ostringstream obtest_oss;                                          \
            obtest_oss << __FILE__ << ":" << __LINE__                               \
                       << ": requirement failed: " << #expr;                        \
            throw ::obtest::TestFailure(obtest_oss.str());                          \
        }                                                                           \
    } while (false)

#define OBTEST_REQUIRE_EQ(actual, expected)                                          \
    do {                                                                            \
        auto obtest_actual = (actual);                                               \
        auto obtest_expected = (expected);                                           \
        if (!(obtest_actual == obtest_expected)) {                                   \
            std::ostringstream obtest_oss;                                          \
            obtest_oss << __FILE__ << ":" << __LINE__                               \
                       << ": equality check failed\n"                               \
                       << "  actual expression:   " << #actual << "\n"              \
                       << "  expected expression: " << #expected << "\n"            \
                       << "  actual value:        "                                 \
                       << ::obtest::show(obtest_actual) << "\n"                     \
                       << "  expected value:      "                                 \
                       << ::obtest::show(obtest_expected);                          \
            throw ::obtest::TestFailure(obtest_oss.str());                          \
        }                                                                           \
    } while (false)

inline std::optional<Price> opt_price(Price price) {
    return std::optional<Price>{price};
}

inline std::optional<Price> no_price() {
    return std::nullopt;
}

inline void require_add_accepted(const AddResult& result, const char* context) {
    if (!result.accepted) {
        std::ostringstream oss;
        oss << "add order was rejected unexpectedly: " << context;
        throw TestFailure(oss.str());
    }
}

inline void expect_trade_count(const AddResult& result, std::size_t expected_count) {
    OBTEST_REQUIRE_EQ(result.trades.size(), expected_count);
}

inline void expect_no_trades(const AddResult& result) {
    expect_trade_count(result, 0);
}

template <class Book>
void expect_trade(
    const AddResult& result,
    std::size_t index,
    Price expected_price,
    Qty expected_qty,
    std::optional<OrderId> expected_maker_order_id = std::nullopt,
    std::optional<OrderId> expected_taker_order_id = std::nullopt
) {
    if (index >= result.trades.size()) {
        std::ostringstream oss;
        oss << "missing trade at index " << index
            << ", trade count was " << result.trades.size();
        throw TestFailure(oss.str());
    }

    const Trade& trade = result.trades[index];

    OBTEST_REQUIRE_EQ(trade.price, expected_price);
    OBTEST_REQUIRE_EQ(trade.qty, expected_qty);

    if constexpr (Book::kCheckTradeOrderIds) {
        OBTEST_REQUIRE_EQ(trade.maker_order_id, expected_maker_order_id);
        OBTEST_REQUIRE_EQ(trade.taker_order_id, expected_taker_order_id);
    }
}

template <class Book>
AddResult add_ok(
    Book& book,
    OrderId order_id,
    Side side,
    Price price,
    Qty qty,
    const char* context
) {
    AddResult result = book.add(order_id, side, price, qty);
    require_add_accepted(result, context);
    return result;
}

template <class Book>
void expect_best(
    const Book& book,
    std::optional<Price> expected_bid,
    std::optional<Price> expected_ask
) {
    OBTEST_REQUIRE_EQ(book.best_bid(), expected_bid);
    OBTEST_REQUIRE_EQ(book.best_ask(), expected_ask);
}

template <class Book>
void empty_book_has_no_best_levels() {
    Book book;
    expect_best(book, no_price(), no_price());
}

template <class Book>
void passive_adds_update_best_levels() {
    Book book;

    auto result = add_ok(book, 1, Side::Buy, 100, 10, "buy 1 @ 100");
    expect_no_trades(result);
    expect_best(book, opt_price(100), no_price());

    result = add_ok(book, 2, Side::Buy, 101, 5, "buy 2 @ 101");
    expect_no_trades(result);
    expect_best(book, opt_price(101), no_price());

    result = add_ok(book, 3, Side::Sell, 110, 7, "sell 3 @ 110");
    expect_no_trades(result);
    expect_best(book, opt_price(101), opt_price(110));

    result = add_ok(book, 4, Side::Sell, 109, 1, "sell 4 @ 109");
    expect_no_trades(result);
    expect_best(book, opt_price(101), opt_price(109));

    result = add_ok(book, 5, Side::Buy, 99, 1, "buy 5 @ 99");
    expect_no_trades(result);
    expect_best(book, opt_price(101), opt_price(109));

    result = add_ok(book, 6, Side::Sell, 111, 1, "sell 6 @ 111");
    expect_no_trades(result);
    expect_best(book, opt_price(101), opt_price(109));
}

template <class Book>
void crossing_orders_match_at_resting_price() {
    {
        Book book;

        auto result = add_ok(book, 10, Side::Sell, 105, 5, "resting sell 10 @ 105");
        expect_no_trades(result);

        result = add_ok(book, 11, Side::Buy, 110, 3, "aggressive buy 11 @ 110");
        expect_trade_count(result, 1);
        expect_trade<Book>(result, 0, 105, 3, OrderId{10}, OrderId{11});
        expect_best(book, no_price(), opt_price(105));

        result = add_ok(book, 12, Side::Buy, 105, 2, "aggressive buy 12 @ 105");
        expect_trade_count(result, 1);
        expect_trade<Book>(result, 0, 105, 2, OrderId{10}, OrderId{12});
        expect_best(book, no_price(), no_price());
    }

    {
        Book book;

        auto result = add_ok(book, 20, Side::Buy, 95, 4, "resting buy 20 @ 95");
        expect_no_trades(result);

        result = add_ok(book, 21, Side::Sell, 90, 4, "aggressive sell 21 @ 90");
        expect_trade_count(result, 1);
        expect_trade<Book>(result, 0, 95, 4, OrderId{20}, OrderId{21});
        expect_best(book, no_price(), no_price());
    }
}

template <class Book>
void fifo_priority_at_same_price() {
    Book book;

    auto result = add_ok(book, 1, Side::Sell, 100, 5, "sell 1 @ 100");
    expect_no_trades(result);

    result = add_ok(book, 2, Side::Sell, 100, 5, "sell 2 @ 100");
    expect_no_trades(result);

    result = add_ok(book, 3, Side::Buy, 100, 7, "buy 3 @ 100");
    expect_trade_count(result, 2);
    expect_trade<Book>(result, 0, 100, 5, OrderId{1}, OrderId{3});
    expect_trade<Book>(result, 1, 100, 2, OrderId{2}, OrderId{3});

    expect_best(book, no_price(), opt_price(100));

    const bool cancelled_filled_order = book.cancel(1);
    OBTEST_REQUIRE_EQ(cancelled_filled_order, false);

    result = add_ok(book, 4, Side::Buy, 100, 3, "buy 4 @ 100");
    expect_trade_count(result, 1);
    expect_trade<Book>(result, 0, 100, 3, OrderId{2}, OrderId{4});

    expect_best(book, no_price(), no_price());
}

template <class Book>
void price_priority_across_levels_and_remainder() {
    Book book;

    auto result = add_ok(book, 1, Side::Sell, 101, 3, "sell 1 @ 101");
    expect_no_trades(result);

    result = add_ok(book, 2, Side::Sell, 99, 2, "sell 2 @ 99");
    expect_no_trades(result);

    result = add_ok(book, 3, Side::Sell, 100, 4, "sell 3 @ 100");
    expect_no_trades(result);

    result = add_ok(book, 4, Side::Buy, 100, 10, "buy 4 @ 100");
    expect_trade_count(result, 2);
    expect_trade<Book>(result, 0, 99, 2, OrderId{2}, OrderId{4});
    expect_trade<Book>(result, 1, 100, 4, OrderId{3}, OrderId{4});

    expect_best(book, opt_price(100), opt_price(101));

    result = add_ok(book, 5, Side::Sell, 100, 4, "sell 5 @ 100");
    expect_trade_count(result, 1);
    expect_trade<Book>(result, 0, 100, 4, OrderId{4}, OrderId{5});

    expect_best(book, no_price(), opt_price(101));
}

template <class Book>
void partial_fill_then_cancel_remaining() {
    Book book;

    auto result = add_ok(book, 1, Side::Sell, 100, 10, "sell 1 @ 100");
    expect_no_trades(result);

    result = add_ok(book, 2, Side::Buy, 100, 4, "buy 2 @ 100");
    expect_trade_count(result, 1);
    expect_trade<Book>(result, 0, 100, 4, OrderId{1}, OrderId{2});

    expect_best(book, no_price(), opt_price(100));

    const bool cancelled = book.cancel(1);
    OBTEST_REQUIRE_EQ(cancelled, true);

    expect_best(book, no_price(), no_price());

    result = add_ok(book, 3, Side::Buy, 100, 10, "buy 3 @ 100 after cancel");
    expect_no_trades(result);

    expect_best(book, opt_price(100), no_price());
}

template <class Book>
void aggressive_remainder_rests() {
    Book book;

    auto result = add_ok(book, 1, Side::Sell, 100, 3, "sell 1 @ 100");
    expect_no_trades(result);

    result = add_ok(book, 2, Side::Buy, 105, 5, "buy 2 @ 105");
    expect_trade_count(result, 1);
    expect_trade<Book>(result, 0, 100, 3, OrderId{1}, OrderId{2});

    expect_best(book, opt_price(105), no_price());

    result = add_ok(book, 3, Side::Sell, 105, 2, "sell 3 @ 105");
    expect_trade_count(result, 1);
    expect_trade<Book>(result, 0, 105, 2, OrderId{2}, OrderId{3});

    expect_best(book, no_price(), no_price());
}

template <class Book>
void cancel_existing_order_updates_best_levels() {
    Book book;

    auto result = add_ok(book, 1, Side::Buy, 100, 1, "buy 1 @ 100");
    expect_no_trades(result);

    result = add_ok(book, 2, Side::Buy, 101, 1, "buy 2 @ 101");
    expect_no_trades(result);

    result = add_ok(book, 3, Side::Buy, 99, 1, "buy 3 @ 99");
    expect_no_trades(result);

    expect_best(book, opt_price(101), no_price());

    OBTEST_REQUIRE_EQ(book.cancel(2), true);
    expect_best(book, opt_price(100), no_price());

    OBTEST_REQUIRE_EQ(book.cancel(1), true);
    expect_best(book, opt_price(99), no_price());

    OBTEST_REQUIRE_EQ(book.cancel(3), true);
    expect_best(book, no_price(), no_price());

    result = add_ok(book, 10, Side::Sell, 110, 1, "sell 10 @ 110");
    expect_no_trades(result);

    result = add_ok(book, 11, Side::Sell, 109, 1, "sell 11 @ 109");
    expect_no_trades(result);

    expect_best(book, no_price(), opt_price(109));

    OBTEST_REQUIRE_EQ(book.cancel(11), true);
    expect_best(book, no_price(), opt_price(110));

    OBTEST_REQUIRE_EQ(book.cancel(10), true);
    expect_best(book, no_price(), no_price());
}

template <class Book>
void cancel_missing_order_is_noop() {
    Book book;

    auto result = add_ok(book, 1, Side::Buy, 100, 5, "buy 1 @ 100");
    expect_no_trades(result);

    const bool cancelled = book.cancel(999999);
    OBTEST_REQUIRE_EQ(cancelled, false);

    expect_best(book, opt_price(100), no_price());

    result = add_ok(book, 2, Side::Sell, 100, 5, "sell 2 @ 100");
    expect_trade_count(result, 1);
    expect_trade<Book>(result, 0, 100, 5, OrderId{1}, OrderId{2});

    expect_best(book, no_price(), no_price());
}

template <class Book>
void duplicate_order_id_is_rejected_before_matching() {
    Book book;

    auto result = add_ok(book, 1, Side::Buy, 100, 5, "buy 1 @ 100");
    expect_no_trades(result);

    // If this were accepted, it would incorrectly trade against order id 1.
    result = book.add(1, Side::Sell, 90, 5);
    OBTEST_REQUIRE_EQ(result.accepted, false);
    expect_no_trades(result);

    expect_best(book, opt_price(100), no_price());

    result = add_ok(book, 2, Side::Sell, 100, 5, "sell 2 @ 100");
    expect_trade_count(result, 1);
    expect_trade<Book>(result, 0, 100, 5, OrderId{1}, OrderId{2});

    expect_best(book, no_price(), no_price());
}

template <class Book>
void boundary_prices_work_when_enabled() {
    if constexpr (!Book::kHasBoundedPriceRange) {
        return;
    } else {
        Book book;

        auto result = add_ok(
            book,
            1,
            Side::Buy,
            Book::kMinPrice,
            1,
            "buy at min price"
        );
        expect_no_trades(result);

        result = add_ok(
            book,
            2,
            Side::Sell,
            Book::kMaxPrice,
            1,
            "sell at max price"
        );
        expect_no_trades(result);

        expect_best(book, opt_price(Book::kMinPrice), opt_price(Book::kMaxPrice));

        OBTEST_REQUIRE_EQ(book.cancel(1), true);
        expect_best(book, no_price(), opt_price(Book::kMaxPrice));

        OBTEST_REQUIRE_EQ(book.cancel(2), true);
        expect_best(book, no_price(), no_price());

        result = add_ok(
            book,
            3,
            Side::Sell,
            Book::kMaxPrice,
            2,
            "sell at max price again"
        );
        expect_no_trades(result);

        result = add_ok(
            book,
            4,
            Side::Buy,
            Book::kMaxPrice,
            2,
            "buy crossing max price"
        );
        expect_trade_count(result, 1);
        expect_trade<Book>(result, 0, Book::kMaxPrice, 2, OrderId{3}, OrderId{4});

        expect_best(book, no_price(), no_price());

        result = add_ok(
            book,
            5,
            Side::Buy,
            Book::kMinPrice,
            2,
            "buy at min price again"
        );
        expect_no_trades(result);

        result = add_ok(
            book,
            6,
            Side::Sell,
            Book::kMinPrice,
            2,
            "sell crossing min price"
        );
        expect_trade_count(result, 1);
        expect_trade<Book>(result, 0, Book::kMinPrice, 2, OrderId{5}, OrderId{6});

        expect_best(book, no_price(), no_price());
    }
}

template <class Book>
int run_engine_contract_suite(const std::string& suite_name) {
    int passed = 0;
    int failed = 0;

    auto run = [&](const char* test_name, auto test_fn) {
        try {
            test_fn();
            ++passed;
            std::cout << "[PASS] " << suite_name << "." << test_name << "\n";
        } catch (const TestFailure& error) {
            ++failed;
            std::cerr << "[FAIL] " << suite_name << "." << test_name << "\n"
                      << "       " << error.what() << "\n";
        } catch (const std::exception& error) {
            ++failed;
            std::cerr << "[FAIL] " << suite_name << "." << test_name << "\n"
                      << "       unexpected exception: " << error.what() << "\n";
        } catch (...) {
            ++failed;
            std::cerr << "[FAIL] " << suite_name << "." << test_name << "\n"
                      << "       unknown non-standard exception\n";
        }
    };

    run("empty_book_has_no_best_levels", [] {
        empty_book_has_no_best_levels<Book>();
    });

    run("passive_adds_update_best_levels", [] {
        passive_adds_update_best_levels<Book>();
    });

    run("crossing_orders_match_at_resting_price", [] {
        crossing_orders_match_at_resting_price<Book>();
    });

    run("fifo_priority_at_same_price", [] {
        fifo_priority_at_same_price<Book>();
    });

    run("price_priority_across_levels_and_remainder", [] {
        price_priority_across_levels_and_remainder<Book>();
    });

    run("partial_fill_then_cancel_remaining", [] {
        partial_fill_then_cancel_remaining<Book>();
    });

    run("aggressive_remainder_rests", [] {
        aggressive_remainder_rests<Book>();
    });

    run("cancel_existing_order_updates_best_levels", [] {
        cancel_existing_order_updates_best_levels<Book>();
    });

    run("cancel_missing_order_is_noop", [] {
        cancel_missing_order_is_noop<Book>();
    });

    run("duplicate_order_id_is_rejected_before_matching", [] {
        duplicate_order_id_is_rejected_before_matching<Book>();
    });

    run("boundary_prices_work_when_enabled", [] {
        boundary_prices_work_when_enabled<Book>();
    });

    std::cout << "\n" << suite_name << ": "
              << passed << " passed, "
              << failed << " failed\n";

    return failed == 0 ? 0 : 1;
}

} // namespace obtest