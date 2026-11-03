#include "shop/ProductRules.h"

#include <doctest/doctest.h>
#include <string_view>

using namespace ss::shop;

TEST_CASE("收纳袋: 原版无色的与带颜色的都认") {
    CHECK(isBundleItemId("minecraft:bundle"));
    CHECK(isBundleItemId("minecraft:red_bundle"));
    CHECK(isBundleItemId("minecraft:light_blue_bundle"));
    // 命名空间不参与判定：自定义物品也叫 bundle 的话同样拦（宁可多拦一件）
    CHECK(isBundleItemId("myaddon:starter_bundle"));
}

TEST_CASE("收纳袋: 别的物品一个都不能误伤") {
    CHECK_FALSE(isBundleItemId("minecraft:undyed_shulker_box")); // 潜影盒的真实 ID（实测）
    CHECK_FALSE(isBundleItemId("minecraft:red_shulker_box"));
    CHECK_FALSE(isBundleItemId("minecraft:diamond"));
    CHECK_FALSE(isBundleItemId("minecraft:bundle_of_sticks")); // 不是以 bundle 结尾
    CHECK_FALSE(isBundleItemId(""));
    // 只有名字，没有命名空间：也认（配置/日志里偶尔就是这么写的）
    CHECK(isBundleItemId("bundle"));
    // 恰好是后缀但前面没有下划线：仍然算（规则只问“以 bundle 结尾”）
    CHECK(isBundleItemId("minecraft:notabundle"));
}
