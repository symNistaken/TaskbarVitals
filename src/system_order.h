#pragma once
// Pure, portable encoding/decoding and reordering for the System strip indicator order.
// Packed into a 32-bit REG_DWORD as up to 8 x 4-bit nibbles (items 0..7, unused slots = 0xF).
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace system_order {

constexpr size_t kMaxSlots = 8;

inline std::vector<int> DecodeSystemOrder(uint32_t raw, size_t count) {
    std::vector<int> order;
    if (count == 0) return order;
    order.reserve(count);
    for (size_t i = 0; i < kMaxSlots; ++i) {
        const int item = static_cast<int>((raw >> (i * 4)) & 0xFu);
        if (item >= 0 && static_cast<size_t>(item) < count &&
            std::find(order.begin(), order.end(), item) == order.end()) {
            order.push_back(item);
        }
    }
    for (size_t i = 0; i < count; ++i) {
        const int item = static_cast<int>(i);
        if (std::find(order.begin(), order.end(), item) == order.end()) {
            order.push_back(item);
        }
    }
    return order;
}

inline uint32_t EncodeSystemOrder(const std::vector<int>& order) {
    uint32_t value = 0xFFFFFFFFu;
    const size_t n = std::min(order.size(), kMaxSlots);
    for (size_t i = 0; i < n; ++i) {
        const uint32_t nibble = (order[i] >= 0 && order[i] < 15) ? static_cast<uint32_t>(order[i]) : 0xFu;
        value = (value & ~(0xFu << (i * 4))) | (nibble << (i * 4));
    }
    return value;
}

inline bool MoveOrderItem(std::vector<int>& order, int item, int dir) {
    auto pos = std::find(order.begin(), order.end(), item);
    if (pos == order.end()) return false;
    const ptrdiff_t i = pos - order.begin();
    const ptrdiff_t j = i + dir;
    if (j < 0 || j >= static_cast<ptrdiff_t>(order.size())) return false;
    std::swap(order[i], order[j]);
    return true;
}

}  // namespace system_order
