#include "engine_contract_cases.h"
#include "ob/common.h"
#include "ob/map_engine/orderbook.h"
#include "ob/map_engine/adapter.h"

namespace {

#ifndef MAP_ENGINE_TEST_RAW_ENGINE
#define MAP_ENGINE_TEST_RAW_ENGINE MapEngine
#endif

using RawMapEngine = MAP_ENGINE_TEST_RAW_ENGINE;

class MapBookAdapter {
public:
    static constexpr bool kCheckTradeOrderIds = true;
    static constexpr bool kHasBoundedPriceRange = false;

    MapBookAdapter()
        : book_() {}

    obtest::AddResult add(
        obtest::OrderId order_id,
        obtest::Side side,
        obtest::Price price,
        obtest::Qty qty
    ) {
        try {
            const auto native_trades =
                book_.add_order(order_id, to_native_side(side), price, qty);

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
            return cancel_native(book_, order_id);
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
   static ob::Side to_native_side(obtest::Side side) {
    #ifdef MAP_ENGINE_SIDE_TYPE
        using NativeSide = MAP_ENGINE_SIDE_TYPE;
        return side == obtest::Side::Buy ? NativeSide::Buy : NativeSide::Sell;
    #else
        return side == obtest::Side::Buy ? ob::Side::Bid : ob::Side::Ask;
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
    static bool cancel_native(Engine& engine, obtest::OrderId order_id) {
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
                static_cast<obtest::Qty>(trade.quantity),
                static_cast<obtest::OrderId>(trade.passive_order_id),
                static_cast<obtest::OrderId>(trade.aggressive_order_id)
            );
        }

        return result;
    }

private:
    RawMapEngine book_;
};

} // namespace

int main() {
    return obtest::run_engine_contract_suite<MapBookAdapter>(
        "map_engine_test"
    );
}