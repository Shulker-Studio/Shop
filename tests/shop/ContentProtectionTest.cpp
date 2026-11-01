#include "shop/ContentProtection.h"

#include <doctest/doctest.h>

namespace {

using namespace ss::shop;

} // namespace

TEST_CASE("搬运方向：六个 facing_direction 换成六个不同的邻居") {
    Cell const device{10, 20, 30};

    auto const down  = cellInDirection(device, static_cast<int>(FacingDirection::Down));
    auto const up    = cellInDirection(device, static_cast<int>(FacingDirection::Up));
    auto const north = cellInDirection(device, static_cast<int>(FacingDirection::North));
    auto const south = cellInDirection(device, static_cast<int>(FacingDirection::South));
    auto const west  = cellInDirection(device, static_cast<int>(FacingDirection::West));
    auto const east  = cellInDirection(device, static_cast<int>(FacingDirection::East));

    REQUIRE(down.has_value());
    REQUIRE(up.has_value());
    REQUIRE(north.has_value());
    REQUIRE(south.has_value());
    REQUIRE(west.has_value());
    REQUIRE(east.has_value());

    CHECK(*down == Cell{10, 19, 30});
    CHECK(*up == Cell{10, 21, 30});
    CHECK(*north == Cell{10, 20, 29});
    CHECK(*south == Cell{10, 20, 31});
    CHECK(*west == Cell{9, 20, 30});
    CHECK(*east == Cell{11, 20, 30});

    // 六个方向必须正好是 neighboursOf 的那六个邻居，不多不少（偏移表只有那一处）
    auto const around = neighboursOf(device);
    CHECK(*up == around[0]);
    CHECK(*down == around[1]);
    CHECK(*north == around[2]);
    CHECK(*south == around[3]);
    CHECK(*west == around[4]);
    CHECK(*east == around[5]);
}

TEST_CASE("搬运方向：越界的 facing_direction 判不出目标格，交回给调用方放行") {
    Cell const device{0, 64, 0};

    CHECK_FALSE(cellInDirection(device, -1).has_value());
    CHECK_FALSE(cellInDirection(device, 6).has_value());
    CHECK_FALSE(cellInDirection(device, 99).has_value());
}

TEST_CASE("漏斗抽取的源容器：永远是漏斗正上方那一格") {
    CHECK(cellAbove(Cell{3, 70, -5}) == Cell{3, 71, -5});
    CHECK(cellAbove(Cell{0, 0, 0}) == Cell{0, 1, 0});
}

TEST_CASE("搬运判据：目标容器是商店箱就跳过，普通容器一律放行") {
    // 四条搬运路径（漏斗方块抽/塞、漏斗矿车吸、铜傀儡取放、投掷器塞）都调这一个函数：
    // 判据里没有身份、方向、白名单，所以不存在「店主可以拿漏斗搬」或「只拦抽不拦塞」的分支。
    CHECK(shouldSkipContentTransfer(true));
    CHECK_FALSE(shouldSkipContentTransfer(false));
}
