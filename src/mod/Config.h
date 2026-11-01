#pragma once

#include "shop/Economy.h"
#include "shop/ShopSign.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace ll::io {
class Logger;
} // namespace ll::io

namespace ss::shop {

/// 经济后端配置（DESIGN §15 的 `economy` 块）。
struct EconomyConfig {
    /// 后端名，取值见 `economy::Backend`。写成字符串而不是枚举：config.json 里的字面量
    /// 就是这些名字，解析与校验统一走 `economy::parseBackend`。
    ///
    /// 默认 `legacy_money`（2026-11-01 起），**不再有 `auto`**：老默认是「LegacyMoney 探不到
    /// 就换记分板」，等于让账本可能一夜之间换一套而服主不知道。现在必须明确写出来，
    /// 探不到就报错不启用交易（`DESIGN §8.4`）。
    std::string backend = "legacy_money";
    /// 存余额的记分板目标名。
    std::string scoreboardObjective = "money";
    /// 记分板目标不存在时自动建一个（DESIGN §8.4）。默认开：选了 `scoreboard` 就是想要记分板
    /// 记账，让服主为了一个 `dummy` 目标再手打一条命令没有收益。
    bool createObjectiveIfMissing = true;
    /// 店主不在线时是否提前拒绝。**只对不支持离线记账的后端有意义**（2026-11-01 起
    /// legacy_money 与 scoreboard 都支持离线，所以这个开关实际只剩为将来新后端准备的作用）：
    /// 关掉只是不再提前拦，并不会让后端获得给离线玩家记账的能力（DESIGN §8.5）。
    bool requireOwnerOnline = true;
    /// 货币符号，只在这里定义一次（表单与消息都从它取名）。
    std::string currencySymbol = "$";
};

/// 商店税配置（DESIGN §8.9 / §15 的 `tax` 块）。
struct TaxConfig {
    /// 税率（百分比：`0.05` = 5%），默认 0 表示不收税。由店主承担，向下取整到整数。
    ///
    /// 开启它等于要求服主把 LegacyMoney 的 `pay_tax` 手动设为 0：那个值没有任何导出能读，
    /// 本模组只能启动时无条件 WARN（见 `initEconomyService`）。
    double shopTaxRate = 0.0;
    /// 税的去向。P0 只认 `void`（销毁），字段先占位：写别的值**不生效**，
    /// 指向税收账户是 P1（DESIGN §8.9）。
    std::string taxDestination = "void";
};

/// config.json 的内存形态。聚合体，字段名就是配置键名。
struct ShopConfig {
    /// 配置结构版本。`ll::config::loadConfig` 用它判断是否需要做默认值迁移，
    /// 所以它是契约的一部分，不能删。
    int                      version                     = 1;
    std::string              adminShopName               = "Admin Shop";
    std::vector<std::string> shopContainers              = {"minecraft:chest", "minecraft:trapped_chest"};
    int                      maxShopAmount               = 3456;
    bool                     blockSellPriceHigherThanBuy = true;
    /// 部分成交：箱子里的货、余额或背包不够时按实际可用量成交，而不是整笔拒绝（DESIGN §10.3）。
    bool                     allowPartialTransactions    = true;
    /// 创造模式玩家不参与交易（DESIGN §10.4）。
    bool                     ignoreCreativeMode          = true;
    /// 店主能不能打开自己的商店箱（DESIGN §9.2 / §15）。关掉后连店主也只能找 OP，
    /// 但**不影响拆箱**：拆掉箱子本来就是店主的权利（拆了即关店）。
    bool                     allowOwnerOpenChest        = true;
    /// 同一玩家对同一块商店牌的连点冷却（毫秒），对齐 DESIGN §10.4。
    int                      shopInteractionCooldownMs   = 250;
    EconomyConfig            economy;
    TaxConfig                tax;
};

/// 读取并校验 config.json；文件不存在时按默认值写出一份。
///
/// 返回空表示配置非法（或无法解析）。调用方必须放弃启用，不能带着半份配置继续跑。
std::optional<ShopConfig> loadShopConfig(std::filesystem::path const& path, ll::io::Logger& logger);

/// 解析器与绑定判定只需要其中三项。
ShopSignConfig toSignConfig(ShopConfig const& config);

} // namespace ss::shop
