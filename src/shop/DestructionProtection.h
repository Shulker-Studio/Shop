#pragma once

#include "shop/ContainerFinder.h"

#include <span>
#include <vector>

namespace ss::shop {

/// 引擎给的「这次要毁掉的格子」+ 每一格的判定结果（是不是被商店占用）。
///
/// 判定本身要读方块实体（牌面文本、附着容器），只有适配层做得了；纯逻辑层只认这一对。
struct DestructionTarget {
    Cell pos{};
    /// 这一格是商店牌或商店箱（DESIGN §9.3）。为真表示它是保护对象。
    bool shopBlock = false;
};

/// 从引擎的目标列表里挑出**要放过**的格子（DESIGN §6.2 H3a–H3d、ADR-0004）。
///
/// 爆炸拿它把商店方块从 `ExplosionStartedEvent::mBlocks` 里剔除，凋灵与火焰拿它决定
/// 跳过哪一格：**只是这一格不动，不是取消整场爆炸/推动/燃烧**——旁边的方块必须照常被毁。
/// ADR-0004 的全部内容就是这个区别，所以「剔除 ≠ 取消」只写在这里，hook 里只做转发。
///
/// 结果按 `targets` 的顺序给出，同一格只出现一次（调用方的列表是集合，这里不再去重）。
std::vector<Cell> cellsToSpare(std::span<DestructionTarget const> targets);

/// 目标格上挂着哪些牌子：附着点正好落在 `target` 的那些（DESIGN §6.2 H3e）。
///
/// 拆掉商店牌**依附的那一格**会让牌子物理掉落、商店随之消失——这是非店主绕过
/// 「不能拆牌」的真实路径。注意这里问的是「谁的附着格是这一格」，不是「这一格是不是容器」：
/// 牌面「像商店牌」的判据不看附着容器（§9.2），所以一块挂在石头墙上的商店牌同样要保护
/// 它背后的石头。
///
/// 站立牌看下方、吊牌看上方、墙牌看向背面的反方向，几何口径只有 `attachedCellOfSign` 一处，
/// 这里不另算一份偏移。结果按 `signs` 的顺序给出。
std::vector<SignRef> signsAttachedTo(Cell target, std::span<SignRef const> signs);

} // namespace ss::shop
