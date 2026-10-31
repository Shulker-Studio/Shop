#pragma once

#include "shop/Economy.h"

#include <string>
#include <string_view>

class Player;

namespace ss::shop {

/// 当前生效的经济后端。
///
/// 这个模块管两件与「钱在谁手上」直接相关的事：按配置选定后端（含降级），
/// 以及账户 → 在线玩家的查询。后者不属于钱，但两个后端与交易服务都要用它。
///
/// 后端是**懒解析**的：`enable()` 的时候世界往往还没起来，拿不到 `Level`，
/// 所以第一次真正要用钱的时候才按配置选后端，选出来的就缓存下来。
/// 一个都选不出来时返回空实现（`available()` 为 false）：表单会写明「没有可用的经济后端」，
/// 交易在 `prepareBuy` 里就被拒，不会走到扣款。
economy::EconomyProvider& economyBackend();

/// 有没有真正能记账的后端（空实现不算）。表单用它决定「余额」显示真值还是占位。
bool economyBackendAvailable();

/// 账户此刻在不在线。记分板后端要拿它判断「这个账户现在能不能记账」，
/// 交易服务要拿它判断「店主不在线时这笔生意还能不能做」（DESIGN §8.5）。
Player* onlinePlayer(std::string_view xuid);

/// 丢掉已选定的后端，下次用到时按（可能已经改过的）配置重选。模组关闭时调用。
void resetEconomy();

} // namespace ss::shop
