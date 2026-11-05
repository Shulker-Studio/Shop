#pragma once

#include "core/economy/Economy.h"
#include "core/sign/ShopSign.h"

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

/// 交易流水配置（`p1-transaction-log` spec D4 / D5 的 `transactionLog` 块）。
struct TransactionLogConfig {
    /// 总开关。关掉之后**两个文件一起停**（业务流水与 `discarded` 都不写）：
    /// 它们只在一个入口上（`appendLogLines`），不存在「只关一个」的组合。
    /// 关掉不影响交易：流水本来就不是交易的一部分。
    bool enabled = true;
    /// 超过几天压成 `.jsonl.gz`：`0` = 永不压缩，默认 `7`。**负数非法**（加载时拒）。
    ///
    /// **只压缩、永不删除**（spec D4）：删数据没有回头路，要清盘得服主自己 `rm` 一次——
    /// 那种操作必须是人做出的决定，不该由插件替他做。
    int compressAfterDays = 7;
};

/// config.json 的内存形态。聚合体，字段名就是配置键名。
struct ShopConfig {
    /// 配置结构版本：只用来记录「这份文件是哪一版结构」，以及将来做**语义**迁移时分支用。
    ///
    /// **加字段不需要改它**：加载时一律拿默认值补齐（`mergeConfigDefaults`），
    /// 缺的键按默认值跑并回写，所以「忘了 bump 版本」不会再让模组拒绝加载。
    /// 反过来说，也不要指望它拦住手写错的配置——那是 `validate` 的事。
    int                      version                     = 1;
    std::string              adminShopName               = "Admin Shop";
    std::vector<std::string> shopContainers              = {"minecraft:chest", "minecraft:trapped_chest"};
    /// 禁售物品：一组**正则**，整串匹配物品 ID（`minecraft:bedrock`）。命中的物品不能当商品，
    /// 装进潜影盒里嵌套也不行（递归看）。**只管玩家开的店**，管理员店不受影响。
    ///
    /// 用法与理由见 DESIGN §5.3。这里存**原文**（要跟着 config.json 序列化），编译成
    /// `ItemBanList` 只做一次、放在 `Shop` 里；写不成正则会拒绝加载（见 `Config.cpp`）。
    std::vector<std::string> bannedItems;
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
    TransactionLogConfig     transactionLog;
};

/// 读取并校验 config.json；文件不存在时按默认值写出一份。
///
/// 返回空表示配置非法（或无法解析）。调用方必须放弃启用，不能带着半份配置继续跑。
std::optional<ShopConfig> loadShopConfig(std::filesystem::path const& path, ll::io::Logger& logger);

/// 解析器与绑定判定只需要其中三项。
ShopSignConfig toSignConfig(ShopConfig const& config);

} // namespace ss::shop
