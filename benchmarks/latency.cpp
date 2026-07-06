#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

#if (defined(LATENCY_ENGINE_BITMAP) && defined(LATENCY_ENGINE_MAP)) || \
    (!defined(LATENCY_ENGINE_BITMAP) && !defined(LATENCY_ENGINE_MAP))
#error "Define exactly one of LATENCY_ENGINE_BITMAP or LATENCY_ENGINE_MAP."
#endif

#if defined(LATENCY_ENGINE_BITMAP)

#if __has_include("ob/bitmap_engine/ladder.h")
#include "ob/bitmap_engine/ladder.h"
#else
#error "Could not find bitmap engine header: ob/bitmap_engine/ladder.h"
#endif

#ifndef LATENCY_ENGINE_TYPE
#define LATENCY_ENGINE_TYPE ob::bitmap_engine::BitmapOrderBook
#endif

static constexpr const char* kEngineName = "BitmapEngine";

#elif defined(LATENCY_ENGINE_MAP)

#if __has_include("ob/map_engine/orderbook.h")
#include "ob/map_engine/orderbook.h"
#else
#error "Could not find map engine header: ob/map_engine/orderbook.h"
#endif

#ifndef LATENCY_ENGINE_TYPE
#define LATENCY_ENGINE_TYPE map_engine::OrderBook
#endif

static constexpr const char* kEngineName = "MapEngine";

#endif

namespace latency_bench {

using RawEngine = LATENCY_ENGINE_TYPE;

using OrderId = ob::OrderId;
using Price = ob::Price;
using Qty = ob::Quantity;

static constexpr Price kBenchmarkMinPrice = 1;
static constexpr Price kBenchmarkMaxPrice = 1'000'000;
static constexpr Price kBenchmarkMidPrice =
    (kBenchmarkMinPrice + kBenchmarkMaxPrice) / 2;

enum class BenchSide {
    Buy,
    Sell
};

enum class OperationKind {
    Add,
    Cancel
};

struct Operation {
    OperationKind kind = OperationKind::Add;
    OrderId order_id = 0;
    BenchSide side = BenchSide::Buy;
    Price price = 0;
    Qty qty = 0;
};

struct Config {
    std::size_t measured_ops = 200'000;
    std::size_t warmup_ops = 20'000;
    std::uint64_t seed = 42;
    bool markdown = false;
};

struct ApplyResult {
    std::size_t trade_records = 0;
    bool cancel_hit = false;
};

struct Stats {
    std::string engine;

    std::size_t measured_ops = 0;
    std::size_t add_ops = 0;
    std::size_t cancel_ops = 0;
    std::size_t cancel_hits = 0;
    std::size_t trade_records = 0;

    double total_ms = 0.0;
    double avg_ns = 0.0;
    double ops_per_second = 0.0;

    long long min_ns = 0;
    long long p50_ns = 0;
    long long p95_ns = 0;
    long long p99_ns = 0;
    long long max_ns = 0;
};

// Prevents over-aggressive optimization in benchmark builds.
static volatile std::size_t g_sink = 0;

class XorShift64 {
public:
    explicit XorShift64(std::uint64_t seed)
        : state_(seed == 0 ? 0x9E3779B97F4A7C15ULL : seed) {}

    std::uint64_t next() {
        std::uint64_t x = state_;
        x ^= x >> 12;
        x ^= x << 25;
        x ^= x >> 27;
        state_ = x;
        return x * 2685821657736338717ULL;
    }

    std::uint64_t uniform(std::uint64_t lo, std::uint64_t hi) {
        return lo + (next() % (hi - lo + 1));
    }

private:
    std::uint64_t state_;
};

class EngineAdapter {
public:
    EngineAdapter()
        : engine_(make_engine()) {}

    std::size_t add(
        OrderId order_id,
        BenchSide side,
        Price price,
        Qty qty
    ) {
        return add_native(
            engine_,
            order_id,
            to_native_side(side),
            price,
            qty
        );
    }

    bool cancel(OrderId order_id) {
        return cancel_native(engine_, order_id);
    }

private:
    static RawEngine make_engine() {
#if defined(LATENCY_ENGINE_BITMAP)
        return RawEngine{ob::bitmap_engine::LadderConfig{
            kBenchmarkMinPrice,
            kBenchmarkMaxPrice,
            1
        }};
#else
        return RawEngine{};
#endif
    }

    static ob::Side to_native_side(BenchSide side) {
        return side == BenchSide::Buy ? ob::Side::Bid : ob::Side::Ask;
    }

    template <typename Engine>
    static std::size_t add_native(
        Engine& engine,
        OrderId order_id,
        ob::Side side,
        Price price,
        Qty qty
    ) {
        /*
            Supports your current APIs:

            Map engine:
                std::vector<map_engine::Trade> submit(...)

            Bitmap engine:
                ob::OrderStatus submit(..., std::vector<ob::TradeRec>&)

            Also supports compatibility APIs:
                add_order(...) returning vector-like trades
                add_order(...) returning void
        */

        if constexpr (
            requires(Engine& e) {
                e.submit(order_id, side, price, qty);
            }
        ) {
            auto trades = engine.submit(order_id, side, price, qty);
            return trades.size();
        } else if constexpr (
            requires(Engine& e, std::vector<ob::TradeRec>& trades) {
                e.submit(order_id, side, price, qty, trades);
            }
        ) {
            std::vector<ob::TradeRec> trades;
            const ob::OrderStatus status =
                engine.submit(order_id, side, price, qty, trades);

            if (status != ob::OrderStatus::Success) {
                return 0;
            }

            return trades.size();
        } else if constexpr (
            requires(Engine& e) {
                e.add_order(order_id, side, price, qty);
            }
        ) {
            if constexpr (
                std::is_void_v<
                    decltype(engine.add_order(order_id, side, price, qty))
                >
            ) {
                engine.add_order(order_id, side, price, qty);
                return 0;
            } else {
                auto trades = engine.add_order(order_id, side, price, qty);
                return trades.size();
            }
        } else {
            static_assert(
                sizeof(Engine) == 0,
                "Engine must provide submit(...) or add_order(...)"
            );
        }
    }

    template <typename Engine>
    static bool cancel_native(Engine& engine, OrderId order_id) {
        if constexpr (
            std::is_void_v<decltype(engine.cancel_order(order_id))>
        ) {
            engine.cancel_order(order_id);
            return true;
        } else {
            return static_cast<bool>(engine.cancel_order(order_id));
        }
    }

private:
    RawEngine engine_;
};

std::vector<Operation>
generate_workload(std::size_t total_ops, std::uint64_t seed) {
    XorShift64 rng(seed);

    std::vector<Operation> ops;
    ops.reserve(total_ops);

    std::vector<OrderId> likely_resting_ids;
    likely_resting_ids.reserve(total_ops / 2 + 1);

    OrderId next_order_id = 1;

    constexpr std::uint64_t kCancelPercent = 10;
    constexpr std::uint64_t kAggressivePercent = 20;
    constexpr Price kPassiveDepth = 2'000;
    constexpr Price kAggressiveDepth = 50;

    for (std::size_t i = 0; i < total_ops; ++i) {
        const bool can_cancel = !likely_resting_ids.empty();
        const bool do_cancel =
            can_cancel && rng.uniform(1, 100) <= kCancelPercent;

        if (do_cancel) {
            const std::size_t index =
                static_cast<std::size_t>(
                    rng.uniform(0, likely_resting_ids.size() - 1)
                );

            const OrderId order_id = likely_resting_ids[index];

            likely_resting_ids[index] = likely_resting_ids.back();
            likely_resting_ids.pop_back();

            ops.push_back(Operation{
                OperationKind::Cancel,
                order_id,
                BenchSide::Buy,
                0,
                0
            });

            continue;
        }

        const bool is_buy = rng.uniform(0, 1) == 0;
        const bool aggressive =
            rng.uniform(1, 100) <= kAggressivePercent;

        Price price = 0;

        if (is_buy) {
            if (aggressive) {
                price = kBenchmarkMidPrice
                    + 1
                    + static_cast<Price>(rng.uniform(0, kAggressiveDepth));
            } else {
                price = kBenchmarkMidPrice
                    - 1
                    - static_cast<Price>(rng.uniform(0, kPassiveDepth));
            }
        } else {
            if (aggressive) {
                price = kBenchmarkMidPrice
                    - 1
                    - static_cast<Price>(rng.uniform(0, kAggressiveDepth));
            } else {
                price = kBenchmarkMidPrice
                    + 1
                    + static_cast<Price>(rng.uniform(0, kPassiveDepth));
            }
        }

        const Qty qty = static_cast<Qty>(rng.uniform(1, 50));
        const OrderId order_id = next_order_id++;

        ops.push_back(Operation{
            OperationKind::Add,
            order_id,
            is_buy ? BenchSide::Buy : BenchSide::Sell,
            price,
            qty
        });

        if (!aggressive) {
            likely_resting_ids.push_back(order_id);
        }
    }

    return ops;
}

ApplyResult apply_operation(EngineAdapter& engine, const Operation& op) {
    if (op.kind == OperationKind::Add) {
        return ApplyResult{
            engine.add(op.order_id, op.side, op.price, op.qty),
            false
        };
    }

    return ApplyResult{
        0,
        engine.cancel(op.order_id)
    };
}

long long percentile(
    const std::vector<long long>& sorted_samples,
    double percentile_value
) {
    if (sorted_samples.empty()) {
        return 0;
    }

    const double scaled =
        (percentile_value / 100.0)
        * static_cast<double>(sorted_samples.size() - 1);

    const std::size_t index = static_cast<std::size_t>(scaled);
    return sorted_samples[index];
}

Stats run_benchmark(const Config& config) {
    if (config.measured_ops == 0) {
        throw std::invalid_argument("--ops must be greater than zero");
    }

    const std::size_t total_ops =
        config.warmup_ops + config.measured_ops;

    const std::vector<Operation> workload =
        generate_workload(total_ops, config.seed);

    EngineAdapter engine;

    using Clock = std::chrono::steady_clock;

    std::vector<long long> samples_ns;
    samples_ns.reserve(config.measured_ops);

    Stats stats;
    stats.engine = kEngineName;
    stats.measured_ops = config.measured_ops;

    bool total_timer_started = false;
    Clock::time_point total_start{};
    Clock::time_point total_end{};

    for (std::size_t i = 0; i < workload.size(); ++i) {
        const Operation& op = workload[i];
        const bool measured = i >= config.warmup_ops;

        if (!measured) {
            apply_operation(engine, op);
            continue;
        }

        if (!total_timer_started) {
            total_start = Clock::now();
            total_timer_started = true;
        }

        const auto op_start = Clock::now();
        const ApplyResult result = apply_operation(engine, op);
        const auto op_end = Clock::now();

        const auto ns =
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                op_end - op_start
            ).count();

        samples_ns.push_back(ns);

        if (op.kind == OperationKind::Add) {
            ++stats.add_ops;
        } else {
            ++stats.cancel_ops;
            if (result.cancel_hit) {
                ++stats.cancel_hits;
            }
        }

        stats.trade_records += result.trade_records;
    }

    total_end = Clock::now();

    const auto total_ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            total_end - total_start
        ).count();

    std::sort(samples_ns.begin(), samples_ns.end());

    stats.total_ms = static_cast<double>(total_ns) / 1'000'000.0;
    stats.avg_ns =
        static_cast<double>(total_ns)
        / static_cast<double>(config.measured_ops);

    stats.ops_per_second = 1'000'000'000.0 / stats.avg_ns;

    stats.min_ns = samples_ns.front();
    stats.p50_ns = percentile(samples_ns, 50.0);
    stats.p95_ns = percentile(samples_ns, 95.0);
    stats.p99_ns = percentile(samples_ns, 99.0);
    stats.max_ns = samples_ns.back();

    g_sink += stats.trade_records;
    g_sink += stats.cancel_hits;

    return stats;
}

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
        << "  --ops N        Measured operations. Default: 200000\n"
        << "  --warmup N     Warmup operations. Default: 20000\n"
        << "  --seed N       Deterministic workload seed. Default: 42\n"
        << "  --markdown     Print one Markdown table row for README usage\n"
        << "  --help         Show this help\n";
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
        } else if (arg == "--markdown") {
            config.markdown = true;
        } else if (arg == "--ops") {
            config.measured_ops =
                parse_size(require_value(i, "--ops"), "--ops");
        } else if (arg.rfind("--ops=", 0) == 0) {
            config.measured_ops =
                parse_size(arg.substr(6), "--ops");
        } else if (arg == "--warmup") {
            config.warmup_ops =
                parse_size(require_value(i, "--warmup"), "--warmup");
        } else if (arg.rfind("--warmup=", 0) == 0) {
            config.warmup_ops =
                parse_size(arg.substr(9), "--warmup");
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

void print_markdown_row(const Stats& stats) {
    std::cout
        << "| " << stats.engine
        << " | " << stats.measured_ops
        << " | " << std::fixed << std::setprecision(2) << stats.avg_ns
        << " | " << stats.p50_ns
        << " | " << stats.p95_ns
        << " | " << stats.p99_ns
        << " | " << std::fixed << std::setprecision(0)
        << stats.ops_per_second
        << " | " << stats.trade_records
        << " | " << stats.cancel_hits
        << " |\n";
}

void print_human(const Stats& stats) {
    std::cout << "\n";
    std::cout << "Latency benchmark result\n";
    std::cout << "========================\n";
    std::cout << "Engine:          " << stats.engine << "\n";
    std::cout << "Measured ops:    " << stats.measured_ops << "\n";
    std::cout << "Add ops:         " << stats.add_ops << "\n";
    std::cout << "Cancel ops:      " << stats.cancel_ops << "\n";
    std::cout << "Cancel hits:     " << stats.cancel_hits << "\n";
    std::cout << "Trade records:   " << stats.trade_records << "\n";
    std::cout << "Total time:      "
              << std::fixed << std::setprecision(3)
              << stats.total_ms << " ms\n";

    std::cout << "\n";
    std::cout << "Per-operation latency\n";
    std::cout << "---------------------\n";
    std::cout << "Average:         "
              << std::fixed << std::setprecision(2)
              << stats.avg_ns << " ns/op\n";
    std::cout << "Min:             " << stats.min_ns << " ns\n";
    std::cout << "P50:             " << stats.p50_ns << " ns\n";
    std::cout << "P95:             " << stats.p95_ns << " ns\n";
    std::cout << "P99:             " << stats.p99_ns << " ns\n";
    std::cout << "Max:             " << stats.max_ns << " ns\n";
    std::cout << "Throughput:      "
              << std::fixed << std::setprecision(0)
              << stats.ops_per_second << " ops/sec\n";

    std::cout << "\n";
    std::cout << "README Markdown row:\n";
    print_markdown_row(stats);
}

} // namespace latency_bench

int main(int argc, char** argv) {
    try {
        const latency_bench::Config config =
            latency_bench::parse_args(argc, argv);

        const latency_bench::Stats stats =
            latency_bench::run_benchmark(config);

        if (config.markdown) {
            latency_bench::print_markdown_row(stats);
        } else {
            latency_bench::print_human(stats);
        }

        return 0;
    } catch (const std::exception& error) {
        std::cerr << "latency benchmark failed: "
                  << error.what() << "\n";
        return 1;
    }
}