#pragma once

namespace ss::shop {

/// 破坏防护（DESIGN §6.2 H3a–H3d、ADR-0004）：爆炸 / 活塞 / 凋灵 / 火焰都动不了商店箱与商店牌，
/// 而旁边的方块照常被炸掉、被推动、被烧掉——是**把商店方块从目标列表里剔除**，
/// 不是取消整场爆炸/推动/燃烧（Java 版 ChestShop 是后者，这里刻意偏离）。
///
/// 四条都只能挂在引擎内部函数上，所以要手动注册/反注册：
/// - H3a `BlockEventCoordinator::sendEvent`：把商店方块从 `ExplosionStartedEvent::mBlocks` 里删掉；
/// - H3b `PistonBlockActor::_attachedBlockWalker`：路径上遇到商店方块返回 false（推不动）；
/// - H3c `WitherBoss::_destroyBlocks` + `Level::destroyBlock`：凋灵破坏期间逐格剔除
///   （不是提前返回——那样旁边的方块也一块不掉）；
/// - H3d `FireBlock::checkBurn`：火焰不烧商店方块。
///
/// 判定全部落在纯逻辑（`shop/DestructionProtection.*`）与适配层的
/// `attachedShopSignsAt` / `shopSignOwnerAt` 上，这里只做接线：hook 里的分支越少，
/// 真机出问题时越容易判断是「符号没对上」还是「判定不对」。
///
/// H3e（拆掉商店牌依附的那一格）**不在这里**：那条走 `PlayerDestroyBlockEvent`，
/// 与 07 的拆除保护是同一个监听（见 `ContainerProtectionListener.h`），不重复注册事件。
void registerDestructionProtectionHooks();

void unregisterDestructionProtectionHooks();

} // namespace ss::shop
