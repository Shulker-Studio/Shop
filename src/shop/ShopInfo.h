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
    int                         stockUnits = 0;    ///< 可成交份数（库存件数 / 每份件数）
    std::optional<std::int64_t> balance;           ///< 空 = 经济后端还没接上
    std::string                 currencySymbol = "$";
};

/// 表单标题：普通店 `<店主> 的商店`，管理员店 `管理员商店`（DESIGN §10.1）。
std::string shopFormTitle(ShopInfoData const& data);

/// 表单正文的每一行：店主、商品、每份件数、买入价、卖出价、可成交份数、余额。
std::vector<std::string> shopInfoLines(ShopInfoData const& data);

/// 表单正文：`shopInfoLines` 用换行符拼起来，行尾不留换行。
std::string shopFormContent(ShopInfoData const& data);

/// 这个表单给谁看：游客/买家，还是店主/OP。
enum class ShopFormRole : std::uint8_t { Visitor, Manager };

/// 按钮要做的事。本期只有文案，04/05 才给买卖动作接上真实逻辑。
enum class ShopFormAction : std::uint8_t { Buy, Sell, Manage };

struct ShopFormButton {
    std::string    label;
    ShopFormAction action = ShopFormAction::Buy;
};

/// 按钮列表：买家拿到购买/出售（缺哪边价格就没有哪个按钮），店主只有一个管理入口。
///
/// 文案取自 DESIGN §10.1：`购买（1 份 N 个 / B 价）`、`出售（1 份 N 个 / S 价）`、`管理商店`。
std::vector<ShopFormButton> shopFormButtons(ShopInfoData const& data, ShopFormRole role);

} // namespace ss::shop
