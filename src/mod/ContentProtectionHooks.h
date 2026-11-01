#pragma once

namespace ss::shop {

/// 内容防偷（DESIGN §6.2 H2a / H2c / H2d）：漏斗方块、漏斗矿车、铜傀儡、投掷器都动不了
/// 商店箱里的东西。四条路径共用同一个判据——「这次搬运的目标容器是不是商店箱」
/// （适配层的 `isShopBlockAt` + 纯逻辑 `shouldSkipContentTransfer`），不在每处各写一份。
///
/// 五条 hook 都要手动注册/反注册（而且**注册成功与否只能靠真机行为判断**，
/// 见 `DestructionProtectionHooks.h` 里同样的说明）：
/// - H2a-1 `Hopper::_tryPullInItemsFromAboveContainer`：源容器在漏斗正上方，是商店箱就抽不动；
/// - H2a-2 `Hopper::_pushOutItems`：目标容器在 `attachedFace` 那一格，是商店箱就塞不进。
///   这两条同时覆盖**漏斗方块与漏斗矿车**——两种形态共用 `Hopper` actor 的同一套逻辑，
///   一条 hook 管两种，不需要按形态各写一遍。
/// - H2c `TransportItemsGoal::_executeEvent`：铜傀儡走到商店箱旁、即将开始「取」或「放」时
///   丢掉这一个事件。**不碰 `canUse` / `canContinueToUse` / `tick`**，也就没有取消整个 goal，
///   所以它不会变成站桩傀儡；代价是这个拦截点是否真的是搬运闸门，只能在真机确认（12 有验收项）。
/// - H2d `DropperBlockActor::pushOutItems`（入口）+ `DropperBlockActor::getContainerAt`（内层）：
///   投掷器朝向的那一格是商店箱时，只在这一次搬运的作用域内让容器查找交不出箱子——
///   于是原版走「喷到地上」的分支，而不是把整个投掷器停掉。
///
/// 判定全部落在纯逻辑（`shop/ContentProtection.*`）与适配层的 `isShopBlockAt` 上，这里只接线：
/// hook 里的分支越少，真机出问题时越容易判断是「符号没对上」还是「判定不对」。
void registerContentProtectionHooks();

void unregisterContentProtectionHooks();

} // namespace ss::shop
