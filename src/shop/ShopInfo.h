#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ss::shop {

/// 信息表单要显示的全部内容，已经解析与统计好。
///
/// 纯逻辑：不含引擎类型，也不知道谁来点这块牌。文案对齐 DESIGN §10.1。
struct ShopInfoData {
    std::string                 ownerName;         ///< 店主显示名；管理员商店可以留空
    bool                        adminShop = false; ///< 管理员商店（DESIGN §7.4）
    std::string                 itemName;          ///< 商品展示名；容器空时用牌面第 4 行
    int                         amountPerUnit = 0; ///< 一份的件数（牌面第 2 行）
    std::optional<std::int64_t> buyPrice;          ///< 一份的买入价，玩家付给店主
    std::optional<std::int64_t> sellPrice;         ///< 一份的卖出价，店主付给玩家
    int                         stockUnits = 0;    ///< 可成交份数（商店箱里件数 / 每份件数）
    /// 玩家手里的同种商品够几份。卖出方向的限额来源（DESIGN §10.3），
    /// 与 `balance` 一样是「谁在看」的属性，由调用方填；信息表单不显示它。
    int                         heldUnits = 0;
    std::optional<std::int64_t> balance; ///< 空 = 经济后端还没接上
    /// 这家店现在不能交易的原因（店主收不了钱、或服务器没有可用经济后端）。
    /// 非空时不给买卖按钮，正文直接写这条原因，免得玩家点进去才被拒（DESIGN §8.5）。
    std::string unavailableReason;
    /// 货币符号。只由调用方从 `economy.currencySymbol` 填进来，这里不留默认值：
    /// 默认值只能有一处定义，否则配置改了表单不跟着变。
    std::string currencySymbol;
};

/// `$ 10` / `金币 10` / 符号为空时只留 `10`。
std::string moneyText(std::string const& symbol, std::int64_t amount);

/// 往正文里追加一行：不是首行就先补一个换行。各处正文共用，别各自拼一遍。
void appendLine(std::string& content, std::string const& line);

/// 店主收不了钱的原因（DESIGN §8.5）。表单与交易提示共用这一句，别写成两种说法。
std::string ownerOfflineReason();

/// 读不到店主余额的原因。
///
/// 「店主余额是 0」与「读不到店主余额」是两件事（DESIGN §8.2）：前者是正常的
/// 「钱不够」，后者是后端出问题或账户不可记账。混成一句会把后者误报成
/// 「店主资金不足」，玩家申诉时无处可查。
std::string ownerFundsUnknownReason();

/// 没有可用经济后端的原因。
std::string noEconomyReason();

/// 表单标题：普通店 `<店主> 的商店`，管理员店 `管理员商店`（DESIGN §10.1）。
std::string shopFormTitle(ShopInfoData const& data);

/// 表单正文的每一行：店主、商品、每份件数、买入价、卖出价、可成交份数、余额。
std::vector<std::string> shopInfoLines(ShopInfoData const& data);

/// 表单正文：`shopInfoLines` 用换行符拼起来，行尾不留换行。
std::string shopFormContent(ShopInfoData const& data);

/// 这个表单给谁看：游客/买家，还是店主/OP。
enum class ShopFormRole : std::uint8_t { Visitor, Manager };

/// 按钮要做的事。`Buy` 已接真实交易，`Sell` 等 05，`Manage` 等 06。
enum class ShopFormAction : std::uint8_t { Buy, Sell, Manage };

struct ShopFormButton {
    std::string    label;
    ShopFormAction action = ShopFormAction::Buy;
};

/// 按钮列表：买家拿到购买/出售（缺哪边价格就没有哪个按钮），店主只有一个管理入口。
///
/// 店主收不了钱时一个买卖按钮都不给：这不是「不行就不行」，而是提前把必然失败的
/// 路径藏起来（DESIGN §8.5）。
///
/// 文案取自 DESIGN §10.1：`购买（1 份 N 个 / B 价）`、`出售（1 份 N 个 / S 价）`、`管理商店`。
std::vector<ShopFormButton> shopFormButtons(ShopInfoData const& data, ShopFormRole role);

} // namespace ss::shop
