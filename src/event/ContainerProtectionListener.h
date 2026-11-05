#pragma once

namespace ss::shop {

/// 商店箱与商店牌的保护：开箱、攻击、拆除（DESIGN §9.2 的「商店箱保护」那一组）。
///
/// 拆除这条路同时也是 08 的 **H3e**：拆掉商店牌**依附的那一格**（不一定是容器）会让
/// 牌子物理掉落、商店消失，所以 `onDestroyBlock` 里除了 07 的判定，还多判一次
/// 「这一格上挂着谁的商店牌」。两件事落在同一个 `PlayerDestroyBlockEvent` 监听里，
/// 不给同一个事件注册两个监听。
///
/// 爆炸 / 活塞 / 凋灵 / 火焰（H3a–H3d）不在这里，见 `DestructionProtectionHooks.h`。
void registerContainerProtectionListener();

void unregisterContainerProtectionListener();

} // namespace ss::shop
