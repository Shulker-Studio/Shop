#include "shop/SampleSlot.h"

#include <doctest/doctest.h>
#include <optional>
#include <vector>

namespace {

using namespace ss::shop;

/// 54 格的箱子： 只把 `filled` 里的格子当成非空，其余是空的。
auto isEmptyExcept(std::vector<int> const& filled) {
    std::vector<bool> occupied(54, false);
    for (int const slot : filled) {
        occupied[static_cast<std::size_t>(slot)] = true;
    }
    return [occupied](int slot) {
        return slot < 0 || slot >= static_cast<int>(occupied.size())
            || !occupied[static_cast<std::size_t>(slot)];
    };
}

} // namespace

TEST_CASE("样品槽: 0 号槽有东西时就是 0 号槽") {
    auto const index = sampleSlotIndex(54, isEmptyExcept({0}));
    REQUIRE(index.has_value());
    CHECK(*index == 0);
}

TEST_CASE("样品槽: 0 号空、1 号有钻石时样品槽是 1 号 (不是写死的 0 号)") {
    auto const index = sampleSlotIndex(54, isEmptyExcept({1}));
    REQUIRE(index.has_value());
    CHECK(*index == 1);

    // 前面空得越多，样品槽越靠后——规则是「第一个非空槽位」，不是一个固定槽号
    auto const later = sampleSlotIndex(54, isEmptyExcept({7, 8}));
    REQUIRE(later.has_value());
    CHECK(*later == 7);
}

TEST_CASE("样品槽: 第一格以外的格子一律忽略，选中的永远是最靠前的那个") {
    // 0 号有样品、后面塞了几组泥土： 样品槽还是 0 号，后面的格子一点不参与
    auto const withJunk = sampleSlotIndex(54, isEmptyExcept({0, 2, 3, 40}));
    REQUIRE(withJunk.has_value());
    CHECK(*withJunk == 0);

    // 样品槽只是「最靠前的非空格」，它后面有什么都不改变判定
    auto const onlyLaterSlots = sampleSlotIndex(54, isEmptyExcept({5, 6, 7}));
    REQUIRE(onlyLaterSlots.has_value());
    CHECK(*onlyLaterSlots == 5);
}

TEST_CASE("样品槽: 整个箱子空着时没有样品槽") {
    CHECK_FALSE(sampleSlotIndex(54, isEmptyExcept({})).has_value());
    // 0 格容器（理论上不该出现）也不许返回一个越界的下标
    CHECK_FALSE(sampleSlotIndex(0, isEmptyExcept({})).has_value());
}
