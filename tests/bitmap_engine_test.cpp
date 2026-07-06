#include "engine_contract_cases.h"
#include "ob/common.h"
#include "ob/bitmap_engine/ladder.h"
#include "ob/bitmap_engine/adapter.h"

#include <optional>
#include <type_traits>
#include <vector>

namespace {

#ifndef BITMAP_ENGINE_TEST_RAW_ENGINE
#define BITMAP_ENGINE_TEST_RAW_ENGINE BitmapEngine
#endif

using RawBitmapEngine = BITMAP_ENGINE_TEST_RAW_ENGINE;

class BitmapBookAdapter {
public:
    static constexpr bool kCheckTradeOrderIds = true;
    static constexpr bool kHasBoundedPriceRange = true;

    static constexpr obtest::Price kMinPrice = 1;
    static constexpr obtest::Price kMaxPrice = 1'000'000;

    BitmapBookAdapter()
        : book_(make_book()) {}

    obtest::AddResult add(
        obtest::OrderId order_id,
        obtest::Side side,
        obtest::Price price,
        obtest::Qty qty
    ) {
        try {
            const auto native_trades =
                book_.add_order(
                    static_cast<ob::OrderId>(order_id),
                    to_native_side(side),
                    static_cast<ob::Price>(price),
                    static_cast<ob::Quantity>(qty)
                );

            return obtest::AddResult{
                true,
                convert_trades(native_trades)
            };
        } catch (...) {
            return obtest::AddResult{false, {}};
        }
    }

    bool cancel(obtest::OrderId order_id) {
        try {
            return cancel_native(
                book_,
                static_cast<ob::OrderId>(order_id)
            );
        } catch (...) {
            return false;
        }
    }

    std::optional<obtest::Price> best_bid() const {
        return normalize_price(book_.best_bid());
    }

    std::optional<obtest::Price> best_ask() const {
        return normalize_price(book_.best_ask());
    }

private:
    static RawBitmapEngine make_book() {
        if constexpr (
            std::is_constructible_v<
                RawBitmapEngine,
                ob::bitmap_engine::LadderConfig
            >
        ) {
            return RawBitmapEngine{
                ob::bitmap_engine::LadderConfig{
                    static_cast<ob::Price>(kMinPrice),
                    static_cast<ob::Price>(kMaxPrice),
                    1
                }
            };
        } else if constexpr (
            std::is_constructible_v<
                RawBitmapEngine,
                obtest::Price,
                obtest::Price
            >
        ) {
            return RawBitmapEngine{kMinPrice, kMaxPrice};
        } else {
            return RawBitmapEngine{};
        }
    }

    static ob::Side to_native_side(obtest::Side side) {
#ifdef BITMAP_ENGINE_SIDE_TYPE
        using NativeSide = BITMAP_ENGINE_SIDE_TYPE;
        return side == obtest::Side::Buy
            ? NativeSide::Buy
            : NativeSide::Sell;
#else
        return side == obtest::Side::Buy
            ? ob::Side::Bid
            : ob::Side::Ask;
#endif
    }

    template <typename NativePrice>
    static std::optional<obtest::Price>
    normalize_price(const std::optional<NativePrice>& price) {
        if (!price.has_value()) {
            return std::nullopt;
        }

        return static_cast<obtest::Price>(*price);
    }

    template <typename NativePrice>
    static std::optional<obtest::Price>
    normalize_price(const NativePrice& price) {
        return static_cast<obtest::Price>(price);
    }

    template <typename Engine>
    static bool cancel_native(Engine& engine, ob::OrderId order_id) {
        if constexpr (
            std::is_void_v<decltype(engine.cancel_order(order_id))>
        ) {
            engine.cancel_order(order_id);
            return true;
        } else {
            return static_cast<bool>(engine.cancel_order(order_id));
        }
    }

    template <typename NativeTrades>
    static std::vector<obtest::Trade>
    convert_trades(const NativeTrades& native_trades) {
        std::vector<obtest::Trade> result;
        result.reserve(native_trades.size());

        for (const auto& trade : native_trades) {
            result.emplace_back(
                static_cast<obtest::Price>(trade.price),
                static_cast<obtest::Qty>(trade.qty),
                static_cast<obtest::OrderId>(trade.maker_id),
                static_cast<obtest::OrderId>(trade.taker_id)
            );
        }

        return result;
    }

private:
    RawBitmapEngine book_;
};

} // namespace

int main() {
    return obtest::run_engine_contract_suite<BitmapBookAdapter>(
        "bitmap_engine_test"
    );
}