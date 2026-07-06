#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <map>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ob/bitmap_engine/ladder.h"
#include "ob/map_engine/orderbook.h"

namespace differential_test {

using OrderId = ob::OrderId;
using Price = ob::Price;
using Qty = ob::Quantity;

using BitmapBook = ob::bitmap_engine::BitmapOrderBook;
using MapBook = map_engine::OrderBook;

static constexpr Price kMinPrice = 1;
static constexpr Price kMaxPrice = 1'000'000;
static constexpr Price kMidPrice = (kMinPrice + kMaxPrice) / 2;

struct Config {
    std::size_t operations = 100'000;
    std::uint64_t seed = 42;
    bool verbose = false;
};

struct LiveOrder {
    OrderId order_id = 0;
    ob::Side side = ob::Side::Bid;
    Price price = 0;
    Qty qty = 0;
};

struct SubmittedOrder {
    OrderId order_id = 0;
    ob::Side side = ob::Side::Bid;
    Price price = 0;
    Qty qty = 0;
};

struct NormalizedTrade {
    Price price = 0;
    Qty qty = 0;

    friend bool operator==(const NormalizedTrade& a, const NormalizedTrade& b) {
        return a.price == b.price && a.qty == b.qty;
    }

    friend bool operator<(const NormalizedTrade& a, const NormalizedTrade& b) {
        if (a.price != b.price) {
            return a.price < b.price;
        }

        return a.qty < b.qty;
    }
};

struct BookSnapshot {
    std::vector<std::pair<Price, Qty>> bids;
    std::vector<std::pair<Price, Qty>> asks;

    friend bool operator==(const BookSnapshot& a, const BookSnapshot& b) {
        return a.bids == b.bids && a.asks == b.asks;
    }
};

struct ApplyOutput {
    std::vector<NormalizedTrade> trades;
    bool accepted = true;
    bool cancel_result = false;
};

std::string side_to_string(ob::Side side) {
    return side == ob::Side::Bid ? "Bid" : "Ask";
}

std::string trades_to_string(const std::vector<NormalizedTrade>& trades) {
    std::ostringstream out;
    out << "[";

    for (std::size_t i = 0; i < trades.size(); ++i) {
        if (i != 0) {
            out << ", ";
        }

        out << "{price=" << trades[i].price
            << ", qty=" << trades[i].qty
            << "}";
    }

    out << "]";
    return out.str();
}

std::string snapshot_to_string(const BookSnapshot& snapshot) {
    std::ostringstream out;

    out << "bids=[";
    for (std::size_t i = 0; i < snapshot.bids.size(); ++i) {
        if (i != 0) {
            out << ", ";
        }

        out << "{price=" << snapshot.bids[i].first
            << ", qty=" << snapshot.bids[i].second
            << "}";
    }

    out << "], asks=[";
    for (std::size_t i = 0; i < snapshot.asks.size(); ++i) {
        if (i != 0) {
            out << ", ";
        }

        out << "{price=" << snapshot.asks[i].first
            << ", qty=" << snapshot.asks[i].second
            << "}";
    }

    out << "]";

    return out.str();
}

template <typename Trade>
NormalizedTrade normalize_trade(const Trade& trade) {
    NormalizedTrade normalized{};

    if constexpr (requires { trade.price; }) {
        normalized.price = trade.price;
    } else if constexpr (requires { trade.px; }) {
        normalized.price = trade.px;
    } else if constexpr (requires { trade.execution_price; }) {
        normalized.price = trade.execution_price;
    } else {
        static_assert(
            sizeof(Trade) == 0,
            "Trade type must expose price, px, or execution_price"
        );
    }

    if constexpr (requires { trade.qty; }) {
        normalized.qty = trade.qty;
    } else if constexpr (requires { trade.quantity; }) {
        normalized.qty = trade.quantity;
    } else if constexpr (requires { trade.executed_qty; }) {
        normalized.qty = trade.executed_qty;
    } else {
        static_assert(
            sizeof(Trade) == 0,
            "Trade type must expose qty, quantity, or executed_qty"
        );
    }

    return normalized;
}

template <typename TradeContainer>
std::vector<NormalizedTrade>
normalize_trades_from_container(const TradeContainer& trades) {
    std::vector<NormalizedTrade> normalized;
    normalized.reserve(trades.size());

    for (const auto& trade : trades) {
        normalized.push_back(normalize_trade(trade));
    }

    return normalized;
}

class BitmapAdapter {
public:
    BitmapAdapter()
        : book_(ob::bitmap_engine::LadderConfig{kMinPrice, kMaxPrice, 1}) {}

    ApplyOutput submit(
        OrderId order_id,
        ob::Side side,
        Price price,
        Qty qty
    ) {
        std::vector<ob::TradeRec> raw_trades;

        const ob::OrderStatus status =
            book_.submit(order_id, side, price, qty, raw_trades);

        return ApplyOutput{
            normalize_trades_from_container(raw_trades),
            status == ob::OrderStatus::Success,
            false
        };
    }

    ApplyOutput cancel(OrderId order_id) {
        if constexpr (
            std::is_void_v<decltype(book_.cancel_order(order_id))>
        ) {
            book_.cancel_order(order_id);
            return ApplyOutput{{}, true, true};
        } else {
            return ApplyOutput{
                {},
                true,
                static_cast<bool>(book_.cancel_order(order_id))
            };
        }
    }

    BookSnapshot snapshot() const {
        return snapshot_native(book_);
    }

private:
    template <typename Book>
    static BookSnapshot snapshot_native(const Book& book) {
        BookSnapshot snapshot;

        if constexpr (requires { book.snapshot(); }) {
            const auto raw = book.snapshot();
            snapshot.bids = raw.bids;
            snapshot.asks = raw.asks;
        } else if constexpr (requires { book.bids_snapshot(); book.asks_snapshot(); }) {
            snapshot.bids = book.bids_snapshot();
            snapshot.asks = book.asks_snapshot();
        } else if constexpr (requires { book.levels(ob::Side::Bid); }) {
            snapshot.bids = book.levels(ob::Side::Bid);
            snapshot.asks = book.levels(ob::Side::Ask);
        } else {
            /*
                Fallback for projects where no public snapshot API exists.

                Differential trade comparison is still useful, but book-state
                equality cannot be verified unless you expose one of:
                  - snapshot()
                  - bids_snapshot() and asks_snapshot()
                  - levels(ob::Side)
            */
        }

        normalize_snapshot(snapshot);
        return snapshot;
    }

    static void normalize_snapshot(BookSnapshot& snapshot) {
        auto remove_empty = [](std::vector<std::pair<Price, Qty>>& levels) {
            levels.erase(
                std::remove_if(
                    levels.begin(),
                    levels.end(),
                    [](const auto& level) {
                        return level.second == 0;
                    }
                ),
                levels.end()
            );
        };

        remove_empty(snapshot.bids);
        remove_empty(snapshot.asks);

        std::sort(
            snapshot.bids.begin(),
            snapshot.bids.end(),
            [](const auto& a, const auto& b) {
                return a.first > b.first;
            }
        );

        std::sort(
            snapshot.asks.begin(),
            snapshot.asks.end(),
            [](const auto& a, const auto& b) {
                return a.first < b.first;
            }
        );
    }

private:
    BitmapBook book_;
};

class MapAdapter {
public:

    ApplyOutput submit(
        OrderId order_id,
        ob::Side side,
        Price price,
        Qty qty
    ) {
        try {
            const auto raw_trades =
                book_.submit(order_id, side, price, qty);

            return ApplyOutput{
                normalize_trades_from_container(raw_trades),
                true,
                false
            };
        } catch (...) {
            return ApplyOutput{
                {},
                false,
                false
            };
        }
    }

    ApplyOutput cancel(OrderId order_id) {
        if constexpr (
            std::is_void_v<decltype(book_.cancel_order(order_id))>
        ) {
            book_.cancel_order(order_id);
            return ApplyOutput{{}, true, true};
        } else {
            return ApplyOutput{
                {},
                true,
                static_cast<bool>(book_.cancel_order(order_id))
            };
        }
    }

    BookSnapshot snapshot() const {
        return snapshot_native(book_);
    }

private:
    template <typename Book>
    static BookSnapshot snapshot_native(const Book& book) {
        BookSnapshot snapshot;

        if constexpr (requires { book.snapshot(); }) {
            const auto raw = book.snapshot();
            snapshot.bids = raw.bids;
            snapshot.asks = raw.asks;
        } else if constexpr (requires { book.bids_snapshot(); book.asks_snapshot(); }) {
            snapshot.bids = book.bids_snapshot();
            snapshot.asks = book.asks_snapshot();
        } else if constexpr (requires { book.levels(ob::Side::Bid); }) {
            snapshot.bids = book.levels(ob::Side::Bid);
            snapshot.asks = book.levels(ob::Side::Ask);
        } else {
            /*
                Fallback for projects where no public snapshot API exists.

                Differential trade comparison is still useful, but book-state
                equality cannot be verified unless you expose one of:
                  - snapshot()
                  - bids_snapshot() and asks_snapshot()
                  - levels(ob::Side)
            */
        }

        normalize_snapshot(snapshot);
        return snapshot;
    }

    static void normalize_snapshot(BookSnapshot& snapshot) {
        auto remove_empty = [](std::vector<std::pair<Price, Qty>>& levels) {
            levels.erase(
                std::remove_if(
                    levels.begin(),
                    levels.end(),
                    [](const auto& level) {
                        return level.second == 0;
                    }
                ),
                levels.end()
            );
        };

        remove_empty(snapshot.bids);
        remove_empty(snapshot.asks);

        std::sort(
            snapshot.bids.begin(),
            snapshot.bids.end(),
            [](const auto& a, const auto& b) {
                return a.first > b.first;
            }
        );

        std::sort(
            snapshot.asks.begin(),
            snapshot.asks.end(),
            [](const auto& a, const auto& b) {
                return a.first < b.first;
            }
        );
    }

private:
    MapBook book_;
};

std::uint64_t parse_u64(const std::string& text, const char* option_name) {
    std::size_t consumed = 0;
    const std::uint64_t value = std::stoull(text, &consumed);

    if (consumed != text.size()) {
        throw std::invalid_argument(
            std::string("invalid value for ") + option_name + ": " + text
        );
    }

    return value;
}

std::size_t parse_size(const std::string& text, const char* option_name) {
    const std::uint64_t value = parse_u64(text, option_name);

    if (value > static_cast<std::uint64_t>(
            std::numeric_limits<std::size_t>::max()
        )
    ) {
        throw std::invalid_argument(
            std::string("value too large for ") + option_name + ": " + text
        );
    }

    return static_cast<std::size_t>(value);
}

void print_help(const char* argv0) {
    std::cout
        << "Usage: " << argv0 << " [options]\n\n"
        << "Options:\n"
        << "  --ops N       Number of randomized operations. Default: 100000\n"
        << "  --seed N      RNG seed. Default: 42\n"
        << "  --verbose     Print progress and operation details\n"
        << "  --help        Show this help\n";
}

Config parse_args(int argc, char** argv) {
    Config config;

    auto require_value = [&](int& i, const char* option_name) -> std::string {
        if (i + 1 >= argc) {
            throw std::invalid_argument(
                std::string("missing value after ") + option_name
            );
        }

        ++i;
        return argv[i];
    };

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];

        if (arg == "--help") {
            print_help(argv[0]);
            std::exit(0);
        } else if (arg == "--verbose") {
            config.verbose = true;
        } else if (arg == "--ops") {
            config.operations =
                parse_size(require_value(i, "--ops"), "--ops");
        } else if (arg.rfind("--ops=", 0) == 0) {
            config.operations =
                parse_size(arg.substr(6), "--ops");
        } else if (arg == "--seed") {
            config.seed =
                parse_u64(require_value(i, "--seed"), "--seed");
        } else if (arg.rfind("--seed=", 0) == 0) {
            config.seed =
                parse_u64(arg.substr(7), "--seed");
        } else {
            throw std::invalid_argument("unknown argument: " + arg);
        }
    }

    return config;
}

class DifferentialRunner {
public:
    explicit DifferentialRunner(Config config)
        : config_(config),
          rng_(config.seed) {}

    void run() {
        if (config_.operations == 0) {
            throw std::invalid_argument("--ops must be greater than zero");
        }

        for (std::size_t i = 0; i < config_.operations; ++i) {
            run_one(i);
        }

        std::cout
            << "Differential test passed: "
            << config_.operations
            << " operations, seed="
            << config_.seed
            << "\n";
    }

private:
    void run_one(std::size_t index) {
        const bool can_cancel = !live_orders_.empty();
        const bool do_cancel =
            can_cancel && uniform_int(1, 100) <= kCancelPercent;

        if (do_cancel) {
            const std::size_t live_index =
                uniform_index(live_orders_.size());

            const LiveOrder live = live_orders_[live_index];

            erase_live_order(live_index);

            if (config_.verbose) {
                std::cout
                    << "#" << index
                    << " CANCEL id=" << live.order_id
                    << "\n";
            }

            const ApplyOutput bitmap_out =
                bitmap_.cancel(live.order_id);

            const ApplyOutput map_out =
                map_.cancel(live.order_id);

            compare_outputs(
                index,
                "cancel",
                SubmittedOrder{
                    live.order_id,
                    live.side,
                    live.price,
                    live.qty
                },
                bitmap_out,
                map_out
            );

            compare_snapshots(index);

            return;
        }

        const SubmittedOrder order = make_random_order();

        if (config_.verbose) {
            std::cout
                << "#" << index
                << " SUBMIT id=" << order.order_id
                << " side=" << side_to_string(order.side)
                << " price=" << order.price
                << " qty=" << order.qty
                << "\n";
        }

        const ApplyOutput bitmap_out =
            bitmap_.submit(
                order.order_id,
                order.side,
                order.price,
                order.qty
            );

        const ApplyOutput map_out =
            map_.submit(
                order.order_id,
                order.side,
                order.price,
                order.qty
            );

        compare_outputs(index, "submit", order, bitmap_out, map_out);

        if (is_probably_resting(order)) {
            live_orders_.push_back(LiveOrder{
                order.order_id,
                order.side,
                order.price,
                order.qty
            });
        }

        compare_snapshots(index);
    }

    SubmittedOrder make_random_order() {
        const bool bid = uniform_int(0, 1) == 0;
        const bool aggressive =
            uniform_int(1, 100) <= kAggressivePercent;

        Price price = 0;

        if (bid) {
            if (aggressive) {
                price =
                    kMidPrice
                    + 1
                    + static_cast<Price>(uniform_int(0, kAggressiveDepth));
            } else {
                price =
                    kMidPrice
                    - 1
                    - static_cast<Price>(uniform_int(0, kPassiveDepth));
            }
        } else {
            if (aggressive) {
                price =
                    kMidPrice
                    - 1
                    - static_cast<Price>(uniform_int(0, kAggressiveDepth));
            } else {
                price =
                    kMidPrice
                    + 1
                    + static_cast<Price>(uniform_int(0, kPassiveDepth));
            }
        }

        return SubmittedOrder{
            next_order_id_++,
            bid ? ob::Side::Bid : ob::Side::Ask,
            price,
            static_cast<Qty>(uniform_int(1, 50))
        };
    }

    bool is_probably_resting(const SubmittedOrder& order) const {
        if (order.side == ob::Side::Bid) {
            return order.price < kMidPrice;
        }

        return order.price > kMidPrice;
    }

    void erase_live_order(std::size_t index) {
        live_orders_[index] = live_orders_.back();
        live_orders_.pop_back();
    }

    void compare_outputs(
        std::size_t index,
        const char* operation_name,
        const SubmittedOrder& order,
        ApplyOutput bitmap_out,
        ApplyOutput map_out
    ) {
        normalize_trades(bitmap_out.trades);
        normalize_trades(map_out.trades);

        if (bitmap_out.accepted != map_out.accepted) {
            fail(
                index,
                operation_name,
                order,
                "accepted/result mismatch",
                "bitmap accepted=" + bool_to_string(bitmap_out.accepted),
                "map accepted=" + bool_to_string(map_out.accepted)
            );
        }

        if (bitmap_out.cancel_result != map_out.cancel_result) {
            fail(
                index,
                operation_name,
                order,
                "cancel result mismatch",
                "bitmap cancel=" + bool_to_string(bitmap_out.cancel_result),
                "map cancel=" + bool_to_string(map_out.cancel_result)
            );
        }

        if (bitmap_out.trades != map_out.trades) {
            fail(
                index,
                operation_name,
                order,
                "trade mismatch",
                "bitmap trades=" + trades_to_string(bitmap_out.trades),
                "map trades=" + trades_to_string(map_out.trades)
            );
        }
    }

    void compare_snapshots(std::size_t index) {
        const BookSnapshot bitmap_snapshot = bitmap_.snapshot();
        const BookSnapshot map_snapshot = map_.snapshot();

        if (bitmap_snapshot == map_snapshot) {
            return;
        }

        SubmittedOrder dummy{};
        fail(
            index,
            "snapshot",
            dummy,
            "book snapshot mismatch",
            "bitmap " + snapshot_to_string(bitmap_snapshot),
            "map    " + snapshot_to_string(map_snapshot)
        );
    }

    [[noreturn]] void fail(
        std::size_t index,
        const char* operation_name,
        const SubmittedOrder& order,
        const std::string& reason,
        const std::string& bitmap_detail,
        const std::string& map_detail
    ) const {
        std::ostringstream error;

        error
            << "\nDifferential test failed\n"
            << "========================\n"
            << "Operation index: " << index << "\n"
            << "Operation:       " << operation_name << "\n"
            << "Order id:        " << order.order_id << "\n"
            << "Side:            " << side_to_string(order.side) << "\n"
            << "Price:           " << order.price << "\n"
            << "Qty:             " << order.qty << "\n"
            << "Reason:          " << reason << "\n"
            << bitmap_detail << "\n"
            << map_detail << "\n"
            << "Seed:            " << config_.seed << "\n";

        throw std::runtime_error(error.str());
    }

    static void normalize_trades(std::vector<NormalizedTrade>& trades) {
        /*
            Matching engines normally emit trades in deterministic matching
            order. Sorting makes the test robust when two engines report
            equivalent fills with different internal traversal order.
        */
        std::sort(trades.begin(), trades.end());
    }

    static std::string bool_to_string(bool value) {
        return value ? "true" : "false";
    }

    std::uint64_t uniform_int(std::uint64_t lo, std::uint64_t hi) {
        std::uniform_int_distribution<std::uint64_t> dist(lo, hi);
        return dist(rng_);
    }

    std::size_t uniform_index(std::size_t size) {
        if (size == 0) {
            throw std::logic_error("uniform_index called with size zero");
        }

        return static_cast<std::size_t>(uniform_int(0, size - 1));
    }

private:
    static constexpr std::uint64_t kCancelPercent = 10;
    static constexpr std::uint64_t kAggressivePercent = 20;
    static constexpr Price kPassiveDepth = 2'000;
    static constexpr Price kAggressiveDepth = 50;

    Config config_;
    std::mt19937_64 rng_;

    BitmapAdapter bitmap_;
    MapAdapter map_;

    std::vector<LiveOrder> live_orders_;
    OrderId next_order_id_ = 1;
};

} // namespace differential_test

int main(int argc, char** argv) {
    try {
        const differential_test::Config config =
            differential_test::parse_args(argc, argv);

        differential_test::DifferentialRunner runner(config);
        runner.run();

        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}