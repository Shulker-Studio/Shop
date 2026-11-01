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
///
/// 只放 04 真消费的键。探测顺序、目标自动创建（`createObjectiveIfMissing`）、
/// `requireOwnerOnline` 都是 10 的勾选项，这里不提前铺。
struct EconomyConfig {
    /// 后端名，取值见 `economy::Backend`（`legacy_money` / `builtin` 在 10 才实现）。
    /// 写成字符串而不是枚举：config.json 里的字面量就是这些名字，
    /// 解析与校验统一走 `economy::parseBackend`。
    std::string backend = "auto";
    /// 存余额的记分板目标名。
    std::string scoreboardObjective = "money";
    /// 货币符号，只在这里定义一次（表单与消息都从它取名）。
    std::string currencySymbol = "$";
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
};

/// 读取并校验 config.json；文件不存在时按默认值写出一份。
///
/// 返回空表示配置非法（或无法解析）。调用方必须放弃启用，不能带着半份配置继续跑。
std::optional<ShopConfig> loadShopConfig(std::filesystem::path const& path, ll::io::Logger& logger);

/// 解析器与绑定判定只需要其中三项。
ShopSignConfig toSignConfig(ShopConfig const& config);

} // namespace ss::shop
