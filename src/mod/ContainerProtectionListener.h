#pragma once

namespace ss::shop {

/// 商店箱与商店牌的保护：开箱、攻击、拆除（DESIGN §9.2 的「商店箱保护」那一组）。
///
/// 破坏防护（爆炸 / 活塞 / 凋灵 / 火焰）不在这里，见 08。
void registerContainerProtectionListener();

void unregisterContainerProtectionListener();

} // namespace ss::shop
