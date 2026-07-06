#include "ob/common.h"
#include "ob/bitmap_engine/types.h"
#include <iostream>
#include <vector>
#include <numeric>
#include <cmath>
#include <algorithm>
#include <cstdint>
#include <array>
#include <random>
#include <cassert>
#include <memory>

constexpr size_t kBits = 21;
constexpr size_t kSlotSize = 1 << kBits;
constexpr size_t kSlotMask = kSlotSize - 1;
constexpr uint64_t kEmptyKey = 0;
constexpr uint64_t kGrp = 0x9E3779B97F4A7C15ULL;
constexpr size_t kOrderMapMaxLoad  = kSlotSize * 0.7; 

namespace ob::bitmap_engine{

// Exactly 4 MapSlots fit into a standard 64-byte CPU cache line
struct alignas(16) MapSlot {
    ob::OrderId    id;   // 8 bytes
    OrderIndex idx;  // 4 bytes
    uint32_t       pad;  // 4 bytes explicit padding
};

class OrderMap{

private:
    MapSlot* lookup_table = nullptr;
    uint64_t count = 0;
    size_t   total_bytes = 0;

    static size_t fib_hashing(ob::OrderId key);
    size_t find_pos(ob::OrderId id) const;

public:
    OrderMap();
    ~OrderMap();

    // Prevent accidental copying which would cause double munmap free
    OrderMap(const OrderMap&) = delete;
    OrderMap& operator =(const OrderMap&) = delete;

    // Move Constructor
    OrderMap(OrderMap&& other) noexcept;

    // Move Assignment Operator
    OrderMap& operator=(OrderMap&& other) noexcept;

    void insert(ob::OrderId id, OrderIndex idx);
    OrderIndex find(ob::OrderId id) const; // Return kInvalidOrderIndex if not found
    void erase(ob::OrderId id);

    uint64_t size() const { return count; }
    bool is_full() const { return count >= kOrderMapMaxLoad ; }
};

} // namespace ob::bitmap_engine