#include "shop/ShopInfo.h"
#include "shop/StockView.h"

#include <doctest/doctest.h>
#include <limits>
#include <optional>

namespace {

using namespace ss::shop;

} // namespace

TEST_CASE("库存视图：有限就是那个份数，不用哨兵表示「很多」") {
    auto const stock = StockView::makeFinite(3);

    REQUIRE(stock.units.has_value());
    CHECK(*stock.units == 3);
    CHECK_FALSE(stock.unlimited);
    CHECK(stock.valid());
    CHECK(stock.finiteUnits() == std::optional<int>{3});
    CHECK(stock.covers(3));
    CHECK_FALSE(stock.covers(4));
    CHECK_FALSE(stock.empty());
}

TEST_CASE("库存视图：0 份是「空了」，负数按 0 处理") {
    CHECK(StockView::makeFinite(0).empty());
    CHECK_FALSE(StockView::makeFinite(0).covers(1));

    // 负数不是一个合法的份数：按 0 处理，免得它参与 min 之后变成「负的成交份数」
    auto const negative = StockView::makeFinite(-5);
    CHECK(negative.units == std::optional<int>{0});
    CHECK(negative.empty());
}

TEST_CASE("库存视图：无限没有数字，够任何份数") {
    auto const stock = StockView::makeUnlimited();

    CHECK_FALSE(stock.units.has_value());
    CHECK(stock.unlimited);
    CHECK(stock.valid());
    // 无限不能有「几份」这个数：`finiteUnits` 为空，所以它不参与 min（而不是等于 INT_MAX）
    CHECK_FALSE(stock.finiteUnits().has_value());
    CHECK(stock.covers(0));
    CHECK(stock.covers(1));
    CHECK(stock.covers(std::numeric_limits<int>::max()));
    // 「收得下」的无限（卖出方向的商店箱，票 02）也不叫「空了」
    CHECK_FALSE(stock.empty());
}

TEST_CASE("库存视图：隐式转换写出来就是有限份数") {
    StockView const stock = 5;

    CHECK(stock.unlimited == false);
    CHECK(stock.finiteUnits() == std::optional<int>{5});
    CHECK(stock.valid());
}

TEST_CASE("库存视图：两种真相同时存在是非法状态，读的一侧按无限处理") {
    // 工厂造不出这种组合（`makeUnlimited` 不带数字），但成员是公开的：
    // 真的有人写错了也得让 `valid()` 能看出来，并且不许退化成「0 份」把生意卡死。
    auto broken  = StockView::makeUnlimited();
    broken.units = 7;

    CHECK_FALSE(broken.valid());
    CHECK_FALSE(broken.finiteUnits().has_value());
    CHECK(broken.covers(100));
}

TEST_CASE("库存文案：有限写份数，无限不编数字") {
    CHECK(stockUnitsText(StockView::makeFinite(3)) == "3 份");
    CHECK(stockUnitsText(StockView::makeFinite(0)) == "0 份");
    // 一处写「无限」、一处写数字是 DESIGN §10.1 D2 明令禁止的两种真相
    CHECK(stockUnitsText(StockView::makeUnlimited()) == "无限（管理员店，永远有货）");
    CHECK(stockUnitsText(StockView{}) == "0 份");
}
