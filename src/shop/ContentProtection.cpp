#include "shop/ContentProtection.h"

namespace ss::shop {

std::optional<Cell> cellInDirection(Cell device, int facingDirection) {
    auto const around = neighboursOf(device); // 上、下、北、南、西、东
    switch (facingDirection) {
    case static_cast<int>(FacingDirection::Down):
        return around[1];
    case static_cast<int>(FacingDirection::Up):
        return around[0];
    case static_cast<int>(FacingDirection::North):
        return around[2];
    case static_cast<int>(FacingDirection::South):
        return around[3];
    case static_cast<int>(FacingDirection::West):
        return around[4];
    case static_cast<int>(FacingDirection::East):
        return around[5];
    default:
        return std::nullopt;
    }
}

Cell cellAbove(Cell device) { return neighboursOf(device)[0]; }

bool shouldSkipContentTransfer(bool targetIsShopContainer) { return targetIsShopContainer; }

} // namespace ss::shop
