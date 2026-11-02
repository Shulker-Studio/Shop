#include "shop/DestructionProtection.h"

#include <doctest/doctest.h>

#include <vector>

namespace {

using namespace ss::shop;

/// 墙牌： `facing` 是正面朝外的方向，附着点在它的反方向（`attachedCellOfSign`）。
SignRef wallSign(Cell pos, HorizontalFacing facing) { return SignRef{pos, SignForm::Wall, facing}; }

/// 一次破坏的读法： 只写「这格是不是商店方块」，其余字段用默认值。
DestructionTarget target(Cell pos, bool shopBlock) { return DestructionTarget{pos, shopBlock}; }

} // namespace

TEST_CASE("爆炸剔除: 只放过受保护的格子，其余照常炸") {
    std::vector<DestructionTarget> const targets{
        target({0, 0, 0}, false),
        target({1, 0, 0}, true), // 商店箱
        target({2, 0, 0}, false),
        target({3, 0, 0}, true), // 商店牌
    };

    auto const spared = cellsToSpare(targets);

    REQUIRE(spared.size() == 2);
    CHECK(spared[0] == Cell{1, 0, 0});
    CHECK(spared[1] == Cell{3, 0, 0});
    // 剔除 ≠ 取消： 场上 4 格只少了 2 格
    CHECK(targets.size() - spared.size() == 2);
}

TEST_CASE("爆炸剔除: 结果按引擎给的顺序，不会把放过名单重排") {
    std::vector<DestructionTarget> const targets{
        target({2, 2, 2}, true),
        target({0, 0, 0}, true),
        target({1, 1, 1}, true),
    };

    auto const spared = cellsToSpare(targets);

    REQUIRE(spared.size() == 3);
    CHECK(spared[0] == Cell{2, 2, 2});
    CHECK(spared[1] == Cell{0, 0, 0});
    CHECK(spared[2] == Cell{1, 1, 1});
}

TEST_CASE("爆炸剔除: 旁边全是普通方块时一个都不放过") {
    std::vector<DestructionTarget> const targets{target({0, 0, 0}, false), target({1, 0, 0}, false)};

    CHECK(cellsToSpare(targets).empty());
    CHECK(cellsToSpare(std::span<DestructionTarget const>{}).empty());
}

TEST_CASE("依附方块: 站立牌挂在下方那格，拆那格才会掉牌") {
    std::vector<SignRef> const signs{SignRef{{0, 1, 0}, SignForm::Standing, HorizontalFacing::North}};

    auto const attached = signsAttachedTo(Cell{0, 0, 0}, signs);

    REQUIRE(attached.size() == 1);
    CHECK(attached[0].pos == Cell{0, 1, 0});
    // 牌子自己那一格不是「依附方块」： 拆牌走的是 BreakSign，不是这一支
    CHECK(signsAttachedTo(Cell{0, 1, 0}, signs).empty());
}

TEST_CASE("依附方块: 吊牌挂在下方那格的上方 (拆天花板才掉牌)") {
    std::vector<SignRef> const signs{SignRef{{0, 0, 0}, SignForm::HangingFromCeiling, HorizontalFacing::North}};

    REQUIRE(signsAttachedTo(Cell{0, 1, 0}, signs).size() == 1);
    CHECK(signsAttachedTo(Cell{0, -1, 0}, signs).empty());
}

TEST_CASE("依附方块: 墙牌挂在正面的反方向，四个朝向都对得上") {
    // 正面朝北（-Z）→ 背面在 +Z；朝南 → -Z；朝西（-X）→ +X；朝东 → -X
    SignRef const north = wallSign({0, 0, 0}, HorizontalFacing::North);
    SignRef const south = wallSign({0, 0, 0}, HorizontalFacing::South);
    SignRef const west  = wallSign({0, 0, 0}, HorizontalFacing::West);
    SignRef const east  = wallSign({0, 0, 0}, HorizontalFacing::East);

    CHECK(signsAttachedTo(Cell{0, 0, 1}, std::span<SignRef const>{&north, 1}).size() == 1);
    CHECK(signsAttachedTo(Cell{0, 0, -1}, std::span<SignRef const>{&south, 1}).size() == 1);
    CHECK(signsAttachedTo(Cell{1, 0, 0}, std::span<SignRef const>{&west, 1}).size() == 1);
    CHECK(signsAttachedTo(Cell{-1, 0, 0}, std::span<SignRef const>{&east, 1}).size() == 1);

    // 反方向不是依附点
    CHECK(signsAttachedTo(Cell{0, 0, -1}, std::span<SignRef const>{&north, 1}).empty());
}

TEST_CASE("依附方块: 只挑挂在这一格上的牌，别的邻居不算") {
    std::vector<SignRef> const signs{
        wallSign({0, 0, 0}, HorizontalFacing::North),                              // 附着 (0,0,1)
        SignRef{{0, 1, 1}, SignForm::Standing, HorizontalFacing::North},           // 附着 (0,0,1)
        SignRef{{1, 1, 0}, SignForm::Standing, HorizontalFacing::North},           // 附着 (1,0,0)
        SignRef{{0, -1, 2}, SignForm::HangingFromCeiling, HorizontalFacing::North}, // 附着 (0,0,2)
    };

    auto const attached = signsAttachedTo(Cell{0, 0, 1}, signs);

    REQUIRE(attached.size() == 2);
    CHECK(attached[0].pos == Cell{0, 0, 0});
    CHECK(attached[1].pos == Cell{0, 1, 1});
    CHECK(signsAttachedTo(Cell{5, 5, 5}, signs).empty());
}
