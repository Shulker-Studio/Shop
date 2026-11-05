#pragma once

#include "core/economy/Economy.h"

#include <string>
#include <string_view>

class Player;

namespace ss::shop {

struct ShopConfig;

/// 当前生效的经济后端。
///
/// 这个模块管三件与「钱在谁手上」直接相关的事：按配置探测并选定后端（含降级与原因）、
/// 账户 → 在线玩家的查询、以及启动时的那几条一次性警告（税、没有后端）。
///
/// 后端是**懒解析**的：`enable()` 的时候世界往往还没起来，拿不到 `Level`，
/// 所以 `enable()` 只挂一个 `ServerStartedEvent` 监听（世界起来后立刻探一次，把最终选择
/// 打进日志），真正的兜底是第一次要用钱时再探一次。探出来的结论会缓存下来，
/// 包括「一个后端都没有」——否则每次动钱都会重探一次、重刷一遍日志。
/// 服主建好记分板目标后需要重载模组（`/ll reload Shop`，或直接重启）才会重探。
economy::EconomyProvider& economyBackend();

/// 最近一次后端解析的「配置值 → 选用值」文本，与启动日志同一句。
/// 还没解析（世界未就绪）时为空；`/shop status` 直接输出它。
std::string const& economySelectionText();

/// 账户此刻在不在线。记分板后端要拿它判断「这个账户现在能不能记账」，
/// 交易服务要拿它判断「店主不在线时这笔生意还能不能做」（DESIGN §8.5）。
Player* onlinePlayer(std::string_view xuid);

/// 这两个账户现在都能记账吗（DESIGN §8.2）。
///
/// 交易在扣款之前用一次：后端的 `transfer` 是「先扣款再入账，入账失败就把钱加回去」，
/// 而「加回去」本身也可能失败（那就是 `Broken`）。所以两边都读得到账号才开始动钱。
/// `provider` 由调用方传入——检查的必须是即将真的转账的那个后端，不能另取一份。
bool accountsRecordable(economy::EconomyProvider& provider, std::string_view payer, std::string_view receiver);

/// 启用时的一次性接线（`Shop::enable()` 调）：
///
/// 1. 启动告警：商店税与 LegacyMoney `pay_tax` 的冲突（DESIGN §8.9）。那个值跨模组读不到，
///    只能在启动时无条件提醒服主；
/// 2. 挂一个 `ServerStartedEvent` 监听：世界一就绪就探一次，把最终选中的后端写进启动日志
///    （DESIGN §8.4 第 4 条）。没赶上也没关系，首次动钱时还会探一次。
void initEconomyService(ShopConfig const& config);

/// 还没解析出后端时，`/shop status` 该显示的那句话；已解析就返回空串。
///
/// 分开写是因为「世界还没起来」与「世界起来了但解析没跑」是两件事，混成一句会让人误判。
Text economyUnresolvedReason();

/// 丢掉已选定的后端并解开监听，下次用到时按（可能已经改过的）配置重探。模组关闭时调用。
void resetEconomy();

} // namespace ss::shop
