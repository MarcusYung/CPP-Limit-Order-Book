#include "ob/bitmap_engine/order_map.h"
#include <sys/mman.h>
#include <stdexcept>
#include <iostream>

namespace ob::bitmap_engine {

OrderMap::OrderMap() {
    total_bytes = kSlotSize * sizeof(MapSlot); // 2,097,152 * 16 = ~33.5 MB
    
    // Huge Pages + Pre-faulting
    int flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE | MAP_HUGETLB;
    void* ptr = mmap(nullptr, total_bytes, PROT_READ | PROT_WRITE, flags, -1, 0);

    // Fallback if Huge Pages are not reserved in the OS
    if (ptr == MAP_FAILED) {
        std::cerr << "[WARNING] MAP_HUGETLB failed. Falling back to standard 4KB pages.\n";
        
        flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE;
        ptr = mmap(nullptr, total_bytes, PROT_READ | PROT_WRITE, flags, -1, 0);
        
        if (ptr == MAP_FAILED) {
            throw std::runtime_error("OrderMap mmap completely failed.");
        }
    }

    lookup_table = static_cast<MapSlot*>(ptr);

    // mmap guarantees zero-initialization, but we initialize explicitly for safety
    for (size_t i = 0; i < kSlotSize; ++i) {
        lookup_table[i].id = kEmptyKey;
        lookup_table[i].idx = kInvalidOrderIndex;
        lookup_table[i].pad = 0;
    }
}

OrderMap::~OrderMap() {
    if (lookup_table != nullptr && lookup_table != MAP_FAILED) {
        munmap(lookup_table, total_bytes);
    }
}

OrderMap::OrderMap(OrderMap&& other) noexcept: lookup_table(other.lookup_table), count(other.count), total_bytes(other.total_bytes){
    other.lookup_table = nullptr;
    other.count = 0;
    other.total_bytes = 0;
}

OrderMap& OrderMap::operator=(OrderMap&& other) noexcept {
    if (this != &other) {
        if (lookup_table != nullptr && lookup_table != MAP_FAILED) {
            munmap(lookup_table, total_bytes);
        }

        lookup_table = other.lookup_table;
        count = other.count;
        total_bytes = other.total_bytes;

        other.lookup_table = nullptr;
        other.count = 0;
        other.total_bytes = 0;
    }

    return *this;
}

size_t OrderMap::fib_hashing(ob::OrderId key) {
    uint64_t mixer = kGrp * key;
    return mixer >> (64 - kBits);
}

size_t OrderMap::find_pos(ob::OrderId id) const {
    size_t pos = fib_hashing(id);
    size_t start = pos;

    while (lookup_table[pos].id != kEmptyKey) {
        if (lookup_table[pos].id == id) return pos;
        
        pos = (pos + 1) & kSlotMask;
        if (pos == start) return SIZE_MAX;
    }
    return SIZE_MAX;
}

OrderIndex OrderMap::find(ob::OrderId id) const {
    size_t pos = find_pos(id);
    if (pos == SIZE_MAX) return kInvalidOrderIndex;
    return lookup_table[pos].idx;
}

void OrderMap::insert(ob::OrderId id, OrderIndex idx) {
    if (count >= kOrderMapMaxLoad) [[unlikely]] {
        throw std::runtime_error("OrderMap maximum load factor reached");
    }

    if(id == kEmptyKey) [[unlikely]] {
        throw std::invalid_argument("OrderId 0 is reserved by OrderMap");
    }

    size_t pos = fib_hashing(id);
    size_t start = pos;

    while (lookup_table[pos].id != kEmptyKey) {

        if (lookup_table[pos].id == id) {
            lookup_table[pos].idx = idx;
            return;
        }
        pos = (pos + 1) & kSlotMask;
        if (pos == start) return; 
    }
    
    lookup_table[pos].id = id;
    lookup_table[pos].idx = idx;
    count++;
}

void OrderMap::erase(ob::OrderId id) {
    size_t gap = find_pos(id);
    if (gap == SIZE_MAX) return; // Not found

    size_t j = (gap + 1) & kSlotMask;

    while (lookup_table[j].id != kEmptyKey) {
        if (j == gap) break; 

        size_t home = fib_hashing(lookup_table[j].id);

        if (((gap - home) & kSlotMask) < ((j - home) & kSlotMask)) {
            lookup_table[gap] = lookup_table[j];
            gap = j;
        }

        j = (j + 1) & kSlotMask;
    }

    lookup_table[gap].id = kEmptyKey;
    lookup_table[gap].idx = kInvalidOrderIndex;
    count--;
}

} // namespace ob::bitmap_engine