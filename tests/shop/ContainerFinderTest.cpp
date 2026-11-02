#include "shop/ContainerFinder.h"

#include <cstdlib>
#include <doctest/doctest.h>
#include <span>

namespace {

using namespace ss::shop;

} // namespace

TEST_CASE("附着规则: 墙牌看背面") {
    Cell const sign{10, 64, 10};

    // 正面朝北（-Z）→ 背面在 +Z
    CHECK(attachedCellOfSign(sign, SignForm::Wall, HorizontalFacing::North) == Cell{10, 64, 11});
    // 正面朝南 → 背面在 -Z
    CHECK(attachedCellOfSign(sign, SignForm::Wall, HorizontalFacing::South) == Cell{10, 64, 9});
    // 正面朝西（-X）→ 背面在 +X
    CHECK(attachedCellOfSign(sign, SignForm::Wall, HorizontalFacing::West) == Cell{11, 64, 10});
    // 正面朝东 → 背面在 -X
    CHECK(attachedCellOfSign(sign, SignForm::Wall, HorizontalFacing::East) == Cell{9, 64, 10});
}

TEST_CASE("附着规则: 站立牌看下方") {
    Cell const sign{3, 64, 3};
    // 朝向对站立牌没有意义，四种都必须是同一格
    for (auto facing : {HorizontalFacing::North,
                        HorizontalFacing::South,
                        HorizontalFacing::West,
                        HorizontalFacing::East}) {
        CHECK(attachedCellOfSign(sign, SignForm::Standing, facing) == Cell{3, 63, 3});
    }
}

TEST_CASE("附着规则: 吊牌看上方") {
    Cell const sign{3, 64, 3};
    CHECK(attachedCellOfSign(sign, SignForm::HangingFromCeiling, HorizontalFacing::North) == Cell{3, 65, 3});
    CHECK(attachedCellOfSign(sign, SignForm::HangingFromCeiling, HorizontalFacing::West) == Cell{3, 65, 3});
}

TEST_CASE("绑定: 墙牌贴在箱子侧面") {
    ContainerRef chest{{10, 64, 11}, 27, std::nullopt};

    CHECK(bindsTo(SignRef{{10, 64, 10}, SignForm::Wall, HorizontalFacing::North}, chest));
    // 贴在同一格旁边的墙上——附着点不是箱子
    CHECK_FALSE(bindsTo(SignRef{{10, 64, 9}, SignForm::Wall, HorizontalFacing::South}, chest));
    // 站立牌立在箱子旁边的地上——下方不是箱子
    CHECK_FALSE(bindsTo(SignRef{{11, 65, 11}, SignForm::Standing, HorizontalFacing::North}, chest));
    // 站立牌立在箱子顶面
    CHECK(bindsTo(SignRef{{10, 65, 11}, SignForm::Standing, HorizontalFacing::North}, chest));
}

TEST_CASE("绑定: 双箱两半是同一个库存") {
    Cell const halfA{100, 64, 200};
    Cell const halfB{101, 64, 200};

    ContainerRef doubleChest{halfA, 54, halfB};

    // 牌子贴在哪一半都算绑上
    CHECK(bindsTo(SignRef{{100, 64, 201}, SignForm::Wall, HorizontalFacing::South}, doubleChest));
    CHECK(bindsTo(SignRef{{101, 64, 201}, SignForm::Wall, HorizontalFacing::South}, doubleChest));
    // 立在其中一半顶上也算
    CHECK(bindsTo(SignRef{{101, 65, 200}, SignForm::Standing, HorizontalFacing::North}, doubleChest));
    // 立在旁边的地上不算
    CHECK_FALSE(bindsTo(SignRef{{102, 65, 200}, SignForm::Standing, HorizontalFacing::North}, doubleChest));
}

TEST_CASE("邻居: 恰好是六个曼哈顿距离为 1 的格子") {
    Cell const origin{5, 6, 7};
    auto const cells = neighboursOf(origin);

    for (auto const& cell : cells) {
        CHECK(std::abs(cell.x - origin.x) + std::abs(cell.y - origin.y) + std::abs(cell.z - origin.z) == 1);
    }
    for (std::size_t i = 0; i < cells.size(); ++i) {
        for (std::size_t j = i + 1; j < cells.size(); ++j) {
            CHECK(cells[i] != cells[j]);
        }
    }
}

TEST_CASE("绑定冲突: 只看合法商店牌") {
    ContainerRef const chest{{10, 64, 11}, 27, std::nullopt};
    Cell const         self{10, 64, 9}; // 正在编辑的牌子

    NeighbourSign const foreignShop{SignRef{{10, 64, 10}, SignForm::Wall, HorizontalFacing::North}, true};
    CHECK(isBoundByAnotherShopSign(std::span<NeighbourSign const>{&foreignShop, 1}, chest, self));

    // 牌子上写着非法内容： 它已经不是商店，不算占用
    NeighbourSign const brokenSign{foreignShop.sign, false};
    CHECK_FALSE(isBoundByAnotherShopSign(std::span<NeighbourSign const>{&brokenSign, 1}, chest, self));

    // 附着点不是这个箱子（贴在同一面墙的另一格）
    NeighbourSign const otherChest{SignRef{{10, 64, 9}, SignForm::Wall, HorizontalFacing::South}, true};
    CHECK_FALSE(isBoundByAnotherShopSign(std::span<NeighbourSign const>{&otherChest, 1}, chest, self));
}

TEST_CASE("绑定冲突: 编辑自己的店不被自己挡住") {
    ContainerRef const chest{{10, 64, 11}, 27, std::nullopt};
    SignRef const      selfSign{{10, 64, 10}, SignForm::Wall, HorizontalFacing::North};

    NeighbourSign const self{selfSign, true};
    CHECK_FALSE(isBoundByAnotherShopSign(std::span<NeighbourSign const>{&self, 1}, chest, selfSign.pos));
}

TEST_CASE("绑定冲突: 双箱两半算一个库存") {
    Cell const         halfA{100, 64, 200};
    Cell const         halfB{101, 64, 200};
    ContainerRef const doubleChest{halfA, 54, halfB};

    // 牌子贴在另一半上，也算占用了这个库存
    NeighbourSign const onHalfB{SignRef{{101, 64, 201}, SignForm::Wall, HorizontalFacing::South}, true};
    CHECK(isBoundByAnotherShopSign(std::span<NeighbourSign const>{&onHalfB, 1}, doubleChest, Cell{99, 64, 200}));
}

TEST_CASE("绑定冲突: 没有候选牌时不算占用") {
    ContainerRef const chest{{10, 64, 11}, 27, std::nullopt};
    CHECK_FALSE(isBoundByAnotherShopSign(std::span<NeighbourSign const>{}, chest, Cell{10, 64, 9}));
}
