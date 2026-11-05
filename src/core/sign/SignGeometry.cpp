#include "core/sign/SignGeometry.h"

namespace ss::shop {
namespace {

/// 朝向 → 附着点相对牌子的偏移。
Cell attachedOffset(HorizontalFacing facing) {
    switch (facing) {
    case HorizontalFacing::North:
        return {0, 0, 1}; // 正面朝北（-Z），背面在 +Z
    case HorizontalFacing::South:
        return {0, 0, -1};
    case HorizontalFacing::West:
        return {1, 0, 0}; // 正面朝西（-X），背面在 +X
    case HorizontalFacing::East:
        return {-1, 0, 0};
    }
    return {0, 0, 0};
}

} // namespace

Cell attachedCellOfSign(Cell signPos, SignForm form, HorizontalFacing facing) {
    switch (form) {
    case SignForm::Standing:
        return Cell{signPos.x, signPos.y - 1, signPos.z};
    case SignForm::HangingFromCeiling:
        return Cell{signPos.x, signPos.y + 1, signPos.z};
    case SignForm::Wall:
        break;
    }
    Cell const offset = attachedOffset(facing);
    return Cell{signPos.x + offset.x, signPos.y + offset.y, signPos.z + offset.z};
}

bool bindsTo(SignRef const& sign, ContainerRef const& container) {
    Cell const attached = attachedCellOfSign(sign.pos, sign.form, sign.facing);
    if (attached == container.pos) {
        return true;
    }
    return container.pairedPos.has_value() && attached == *container.pairedPos;
}

bool isBoundByAnotherShopSign(std::span<NeighbourSign const> signs, ContainerRef const& container, Cell selfSign) {
    for (auto const& candidate : signs) {
        if (!candidate.isShop || candidate.sign.pos == selfSign) {
            continue;
        }
        if (bindsTo(candidate.sign, container)) {
            return true;
        }
    }
    return false;
}

std::array<Cell, 6> neighboursOf(Cell cell) {
    return {
        Cell{cell.x, cell.y + 1, cell.z},
        Cell{cell.x, cell.y - 1, cell.z},
        Cell{cell.x, cell.y, cell.z - 1},
        Cell{cell.x, cell.y, cell.z + 1},
        Cell{cell.x - 1, cell.y, cell.z},
        Cell{cell.x + 1, cell.y, cell.z},
    };
}

} // namespace ss::shop
