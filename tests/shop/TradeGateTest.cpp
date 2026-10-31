#include "shop/TradeGate.h"

#include <doctest/doctest.h>

namespace {

using namespace ss::shop;

Cell const kSign{10, 64, 10};
Cell const kOther{10, 64, 11};

} // namespace

TEST_CASE("连点冷却：首次点击放行，窗口内重复点击被拒") {
    TradeGate gate{250};

    CHECK(gate.allow("xuid-a", kSign, 1'000));
    CHECK_FALSE(gate.allow("xuid-a", kSign, 1'001));
    CHECK_FALSE(gate.allow("xuid-a", kSign, 1'249));
    // 窗口是「距上次放行不足冷却时长」，第 250ms 起重新放行
    CHECK(gate.allow("xuid-a", kSign, 1'250));
}

TEST_CASE("连点冷却：不同玩家互不影响") {
    TradeGate gate{250};

    CHECK(gate.allow("xuid-a", kSign, 1'000));
    CHECK(gate.allow("xuid-b", kSign, 1'000));
    CHECK_FALSE(gate.allow("xuid-a", kSign, 1'100));
    CHECK_FALSE(gate.allow("xuid-b", kSign, 1'100));
}

TEST_CASE("连点冷却：同一玩家点不同牌子互不影响") {
    TradeGate gate{250};

    CHECK(gate.allow("xuid-a", kSign, 1'000));
    CHECK(gate.allow("xuid-a", kOther, 1'000));
}

TEST_CASE("连点冷却：被拒的点击不刷新窗口") {
    TradeGate gate{250};

    CHECK(gate.allow("xuid-a", kSign, 1'000));
    CHECK_FALSE(gate.allow("xuid-a", kSign, 1'200));
    // 仍然从上一次「放行」算起，而不是从被拒的那次
    CHECK(gate.allow("xuid-a", kSign, 1'250));
}

TEST_CASE("连点冷却：冷却为 0 时永远放行") {
    TradeGate gate{0};

    CHECK(gate.allow("xuid-a", kSign, 1'000));
    CHECK(gate.allow("xuid-a", kSign, 1'000));
    CHECK(gate.allow("xuid-a", kSign, 1'000));
    CHECK(gate.tracked() == 0);
}

TEST_CASE("连点冷却：过期记录会被清理") {
    TradeGate gate{250};

    CHECK(gate.allow("xuid-a", kSign, 1'000));
    CHECK(gate.allow("xuid-b", kSign, 1'000));
    CHECK(gate.tracked() == 2);

    gate.prune(1'100); // 还在窗口内
    CHECK(gate.tracked() == 2);

    gate.prune(1'250); // 正好到期
    CHECK(gate.tracked() == 0);

    // 清理只丢记录，不影响后续放行
    CHECK(gate.allow("xuid-a", kSign, 1'250));
}

TEST_CASE("连点冷却：记录数量有上限，不会无限增长") {
    TradeGate gate{250};

    // 每次换一个牌子坐标：窗口内留下大量互不相干的记录
    for (int i = 0; i < 2'000; ++i) {
        CHECK(gate.allow("xuid-a", Cell{i, 64, 0}, 1'000));
    }
    // 同一个玩家在同一个 tick 内刷不出无限增长的表；超上限后表会被压回阈值内
    CHECK(gate.tracked() <= TradeGate::kMaxTracked);
}
