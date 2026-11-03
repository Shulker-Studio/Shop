#include "shop/SampleSlot.h"

#include <algorithm>
#include <doctest/doctest.h>
#include <optional>
#include <set>
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

TEST_CASE("可卖槽位: 从最后一格往前，而且跳过样品槽") {
    auto const order = saleableSlotOrder(54, 0);
    REQUIRE(order.size() == 53);
    CHECK(order.front() == 53);
    CHECK(order.back() == 1);
    // 样品槽永不出售：它根本不在可卖顺序里
    CHECK(std::find(order.begin(), order.end(), 0) == order.end());
}

TEST_CASE("可卖槽位: 样品槽在中间也只跳它一格，顺序还是倒着的") {
    CHECK(saleableSlotOrder(6, 3) == std::vector<int>{5, 4, 2, 1, 0});
    // 样品槽在最后一格时，倒序遍历的第一个就是它——跳过之后从 4 开始
    CHECK(saleableSlotOrder(5, 4) == std::vector<int>{3, 2, 1, 0});
}

TEST_CASE("可卖槽位: 没有样品槽的概念时就是纯倒序（玩家背包、总件数统计）") {
    CHECK(saleableSlotOrder(4, std::nullopt) == std::vector<int>{3, 2, 1, 0});
}

TEST_CASE("可卖槽位: 空容器与越界的样品槽都不许吞掉别的格") {
    CHECK(saleableSlotOrder(0, std::nullopt).empty());
    CHECK(saleableSlotOrder(-1, 0).empty());
    // 越界的样品槽匹配不上任何一格，于是它不影响结果（不能因此少卖一格）
    CHECK(saleableSlotOrder(3, 9) == std::vector<int>{2, 1, 0});
}

TEST_CASE("可卖槽位: 每一格最多出现一次（重复就会把份数数成两倍）") {
    for (int const slots : {27, 54}) {
        auto const order = saleableSlotOrder(slots, 0);
        CHECK(order.size() == static_cast<std::size_t>(slots - 1));
        CHECK(std::set<int>(order.begin(), order.end()).size() == order.size());
    }
}
