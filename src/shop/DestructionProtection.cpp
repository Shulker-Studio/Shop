#include "shop/DestructionProtection.h"

namespace ss::shop {

std::vector<Cell> cellsToSpare(std::span<DestructionTarget const> targets) {
    std::vector<Cell> spared;
    for (DestructionTarget const& target : targets) {
        if (target.shopBlock) {
            spared.push_back(target.pos);
        }
    }
    return spared;
}

std::vector<SignRef> signsAttachedTo(Cell target, std::span<SignRef const> signs) {
    std::vector<SignRef> attached;
    for (SignRef const& sign : signs) {
        if (attachedCellOfSign(sign.pos, sign.form, sign.facing) == target) {
            attached.push_back(sign);
        }
    }
    return attached;
}

} // namespace ss::shop
