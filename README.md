# C++ Limit Order Book / Matching Engine

A C++ limit order book and matching engine project focused on trading-system style data structures, correctness testing, and latency benchmarking.

This project implements two matching engine designs under a common order book interface:

| Engine | Description |
|---|---|
| `MapEngine` | Reference implementation using `std::map`, `std::list`, and `std::unordered_map`. Designed for simplicity, correctness, and readability. |
| `BitmapEngine` | Bounded-range price ladder using hierarchical bitmaps, contiguous price levels, an intrusive FIFO order queue, a fixed-size order pool, and a custom open-addressing order-id map. |

The goal is to compare a general-purpose map-based design with a more specialized market-infrastructure style design under the same contract tests, differential tests, simulator workloads, and latency benchmarks.

---

## Features

- Limit order submission
- Order cancellation by order id
- Price-time priority matching
- Partial fills
- Aggressive order matching
- Resting remainder insertion
- Best bid / best ask queries
- Trade record generation
- Shared contract tests across engines
- Randomized differential testing between implementations
- Latency benchmarking with average, P50, P95, P99, max latency, and throughput

---

## Engine Implementations

### `MapEngine`

`MapEngine` is the baseline/reference implementation.

It uses:

- `std::map<Price, PriceLevel, std::greater<Price>>` for bids
- `std::map<Price, PriceLevel>` for asks
- `std::list<Order>` for FIFO queues at each price level
- `std::unordered_map<OrderId, OrderLocation>` for order-id lookup and cancellation

This implementation is easier to reason about and serves as a reference engine for correctness testing.

### `BitmapEngine`

`BitmapEngine` is a bounded-range price ladder implementation.

It uses:

- Contiguous arrays of price levels
- Hierarchical bitmaps for best bid / ask discovery
- Intrusive doubly-linked FIFO queues using order indices
- A fixed-size `OrderPool`
- A custom open-addressing `OrderMap`
- Optional huge-page allocation attempt for the order map, with fallback to standard pages

This design avoids tree traversal for best-price lookup and is intended to model a lower-level market infrastructure implementation where the price range and tick size are known in advance.

---

## Matching Semantics

Both engines implement price-time priority:

1. Match against the best available opposite price.
2. At the same price level, match resting orders FIFO.
3. Execute trades at the resting order's price.
4. If the aggressive order has remaining quantity, rest the remainder on the book.

Supported behavior includes:

- Passive adds
- Crossing orders
- Partial fills
- Multi-level sweeps
- FIFO matching at the same price
- Cancellations by order id
- Duplicate order-id rejection
- Best bid / best ask updates

---

## Correctness Testing

The project includes a shared engine contract test suite that validates both engines against the same behavioral requirements.

Covered test cases include:

- Empty book has no best bid / ask
- Passive adds update best levels
- Crossing orders match at resting price
- FIFO priority at the same price level
- Price priority across multiple levels
- Partial fill then cancel remaining quantity
- Aggressive remainder rests on the book
- Cancel existing order updates best levels
- Cancel missing order is a no-op
- Duplicate order id is rejected before matching
- Boundary prices work for bounded engines

Example test commands:

```bash
./map_engine_test
./bitmap_engine_test
```

Example output:

```text
[PASS] map_engine_test.empty_book_has_no_best_levels
[PASS] map_engine_test.passive_adds_update_best_levels
[PASS] map_engine_test.crossing_orders_match_at_resting_price
...
map_engine_test: 11 passed, 0 failed

[PASS] bitmap_engine_test.empty_book_has_no_best_levels
[PASS] bitmap_engine_test.passive_adds_update_best_levels
[PASS] bitmap_engine_test.crossing_orders_match_at_resting_price
...
bitmap_engine_test: 11 passed, 0 failed
```

---

## Differential Testing

In addition to fixed contract tests, the project includes a randomized differential test.

The differential test applies the same randomized sequence of operations to both engines and compares their behavior.

It compares:

- Accepted / rejected order status
- Cancel results
- Trade prices
- Trade quantities
- Optional book snapshots if a snapshot API is exposed

Example:

```bash
./differential_test --ops 100000 --seed 42
```

Example output:

```text
Differential test passed: 100000 operations, seed=42
```

This helps catch implementation-specific bugs that may not be covered by fixed unit tests.

---

## Simulator Workloads

The simulator can generate different market-style event streams.

Currently supported workload profiles:

| Workload | Description |
|---|---|
| `AddOnly` | Pure insertions. Stresses order-map memory and price-level creation. |
| `CancelHeavy` | Adds mixed with cancels. Stresses order-id lookup and queue unlinking. |
| `CrossingHeavy` | Aggressive orders. Stresses the matching loop and trade generation. |

The generated events are used for latency benchmarking and engine comparison.

---

## Latency Benchmark

The benchmark measures per-operation latency over a mixed add/cancel/trade workload.

Reported metrics include:

- Total measured operations
- Add operation count
- Cancel operation count
- Cancel hits
- Trade records generated
- Total elapsed time
- Average latency
- Minimum latency
- P50 latency
- P95 latency
- P99 latency
- Maximum latency
- Throughput

Both engines are benchmarked using the same generated event stream so the results are directly comparable on the same machine.

---

## Benchmark Results

Benchmark configuration:

| Field | Value |
|---|---:|
| Measured operations | 200,000 |
| Add operations | 179,878 |
| Cancel operations | 20,122 |
| Cancel hits | 19,643 |
| Trade records | 38,249 |

Latency summary:

| Engine | Ops | Avg ns/op | P50 ns | P95 ns | P99 ns | Throughput ops/sec | Trade Records | Cancel Hits |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| `MapEngine` | 200,000 | 172.73 | 123 | 190 | 978 | 5,789,255 | 38,249 | 19,643 |
| `BitmapEngine` | 200,000 | 170.77 | 132 | 243 | 670 | 5,855,986 | 38,249 | 19,643 |

Raw benchmark output for `MapEngine`:

```text
Latency benchmark result
========================
Engine:          MapEngine
Measured ops:    200000
Add ops:         179878
Cancel ops:      20122
Cancel hits:     19643
Trade records:   38249
Total time:      34.547 ms

Per-operation latency
---------------------
Average:         172.73 ns/op
Min:             36 ns
P50:             123 ns
P95:             190 ns
P99:             978 ns
Max:             1056704 ns
Throughput:      5789255 ops/sec
```

Raw benchmark output for `BitmapEngine`:

```text
[WARNING] MAP_HUGETLB failed. Falling back to standard 4KB pages.

Latency benchmark result
========================
Engine:          BitmapEngine
Measured ops:    200000
Add ops:         179878
Cancel ops:      20122
Cancel hits:     19643
Trade records:   38249
Total time:      34.153 ms

Per-operation latency
---------------------
Average:         170.77 ns/op
Min:             33 ns
P50:             132 ns
P95:             243 ns
P99:             670 ns
Max:             104389 ns
Throughput:      5855986 ops/sec
```

### Benchmark Interpretation

In this benchmark run, both engines achieved similar average latency and throughput.

`BitmapEngine` showed slightly better average latency and throughput, as well as lower P99 latency in this workload. However, the P50 and P95 numbers were not uniformly better than `MapEngine`, which suggests that the benefit depends on workload shape, price distribution, cache behavior, and system conditions.

The bitmap design is expected to be more beneficial when:

- The price range is bounded
- The tick size is known in advance
- Best bid / ask lookup is frequent
- Tree traversal overhead becomes significant
- Active price levels are reused heavily
- Cancels and best-level updates are common

The map-based design remains competitive for smaller books, sparse price distributions, or workloads where the flexibility of ordered maps is more useful than a bounded price ladder.

---

## Huge Page Note

`BitmapEngine` attempts to allocate the custom order map using Linux huge pages:

```text
MAP_HUGETLB
```

If huge pages are not configured on the system, the engine falls back to standard 4KB pages:

```text
[WARNING] MAP_HUGETLB failed. Falling back to standard 4KB pages.
```

This fallback is expected on many default Linux environments and does not prevent the engine or benchmark from running.

---

## Design Notes

### Order Storage

`MapEngine` stores orders in `std::list` containers at each price level and keeps iterators in an `unordered_map` for cancellation.

`BitmapEngine` stores orders in a fixed-size `OrderPool`. Price levels maintain intrusive linked queues using order indices instead of pointers or list nodes.

### Best Price Lookup

`MapEngine` retrieves best prices from the beginning of ordered maps:

- Best bid: first element of descending bid map
- Best ask: first element of ascending ask map

`BitmapEngine` tracks non-empty price levels in hierarchical bitmaps and uses bit operations to find the best bid or ask.

### Cancellation

Both engines support cancellation by order id.

`MapEngine` performs cancellation by looking up the order's list iterator and removing it from the corresponding price level.

`BitmapEngine` performs cancellation by looking up the order index in its custom order map, unlinking the order from the intrusive price-level queue, clearing bitmap levels if needed, and returning the slot to the free list.

---

## Benchmark Caveats

These numbers should be interpreted as local microbenchmark results rather than universal performance claims.

Latency may vary depending on:

- CPU model
- Compiler version
- Optimization flags
- OS scheduler noise
- CPU frequency scaling
- Cache state
- Memory allocator behavior
- Huge-page availability
- Workload distribution
- Background system load

The benchmark is most useful for comparing both engines under the same event stream on the same machine.

---

## Limitations

This project is an educational / portfolio matching engine and is not production trading software.

Current limitations include:

- Single-threaded matching
- No persistence or recovery
- No network protocol
- No exchange gateway
- No risk checks
- No market data publishing layer
- No snapshot/replay mechanism
- Fixed capacity in `BitmapEngine`
- Bounded price range required for `BitmapEngine`
- Benchmark results depend heavily on hardware and runtime conditions

---

## Future Work

Potential improvements:

- Add public snapshot APIs for both engines
- Add CSV benchmark output
- Add benchmark profiles for add-only, cancel-heavy, crossing-heavy, hot-price, wide-book, and sweep workloads
- Add warm-up iterations before measurement
- Add CPU pinning support
- Add randomized property tests
- Add fuzz testing
- Add memory usage reporting
- Compare additional order-id map implementations
- Add replay from recorded market-style event streams
- Add risk-check layer
- Add market data event generation