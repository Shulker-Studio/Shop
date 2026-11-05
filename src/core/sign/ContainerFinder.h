#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>

namespace ss::shop {

/// 维度内的一格整数坐标。纯逻辑层不认引擎的 `BlockPos`，只认这个。
struct Cell {
    int x = 0;
    int y = 0;
    int z = 0;
};

constexpr bool operator==(Cell const& lhs, Cell const& rhs) {
    return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z;
}

constexpr bool operator!=(Cell const& lhs, Cell const& rhs) { return !(lhs == rhs); }

/// 告示牌的附着形态：决定它贴在/立在/挂在哪一格。
///
/// 侧挂的悬挂牌在基岩版里无法从方块状态判出挂在哪一面墙（`facing_direction`
/// 与墙面垂直），猜错会绑到旁边的另一个容器上，所以这里没有这个形态。
enum class SignForm : std::uint8_t {
    Wall,               ///< 贴在方块侧面
    Standing,           ///< 立在方块顶面
    HangingFromCeiling, ///< 吊在方块底面
};

/// 牌子正面朝外的水平方向（附着点在它的反方向）。只对墙牌有意义。
enum class HorizontalFacing : std::uint8_t { North, South, West, East };

/// 牌子附着的那一格。站立牌/吊牌忽略朝向。
Cell attachedCellOfSign(Cell signPos, SignForm form, HorizontalFacing facing);

/// 一个容器（双箱已合并）。`size` 是合并后的总格数，`pairedPos` 是另一半。
struct ContainerRef {
    Cell                pos{};
    int                 size = 0;
    std::optional<Cell> pairedPos;
};

/// 牌子几何：位置 + 形态 + 朝向。
struct SignRef {
    Cell             pos{};
    SignForm         form   = SignForm::Standing;
    HorizontalFacing facing = HorizontalFacing::North;
};

/// 这块牌是否绑在这个容器上。双箱两半算同一个库存。
bool bindsTo(SignRef const& sign, ContainerRef const& container);

/// 一块候选牌：几何 + 它的正面是不是一块合法商店牌。
struct NeighbourSign {
    SignRef sign{};
    bool    isShop = false;
};

/// 这些牌里有没有**另一块合法商店牌**绑在这个容器上（双箱两半算同一个库存）。
///
/// `selfSign` 是正在编辑的那块牌，跳过它——编辑自己的店不该被自己挡住。
bool isBoundByAnotherShopSign(std::span<NeighbourSign const> signs, ContainerRef const& container, Cell selfSign);

/// 一格的六个正邻居，顺序固定：上、下、北、南、西、东。
std::array<Cell, 6> neighboursOf(Cell cell);

} // namespace ss::shop
