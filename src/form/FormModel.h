#pragma once

#include "core/shop/ShopFormRole.h"
#include "core/shop/ShopInfoData.h"
#include "util/i18n/Text.h"

#include <cstdint>
#include <vector>

namespace ss::shop {

/// 表单标题：普通店 `<店主> 的商店`，管理员店 `管理员商店`（DESIGN §10.1）。
Text shopFormTitle(ShopInfoData const& data);

/// 表单正文的每一行：店主、商品、每份件数、买入价、卖出价、可成交份数、余额。
std::vector<Text> shopInfoLines(ShopInfoData const& data);

/// 表单正文：`shopInfoLines` 用换行符拼起来，行尾不留换行。
std::vector<Text> shopFormContent(ShopInfoData const& data);

/// 按钮要做的事。
///
/// 每个动作一个值，**不靠按钮下标猜**：`SimpleForm` 回调给的是下标，下标随按钮顺序变，
/// 停业时「管理商店」可能排在第 0 位也可能不在。
enum class ShopFormAction : std::uint8_t {
    Buy,             ///< 弹买入份数表单
    Sell,            ///< 弹出份数表单
    Manage,          ///< 进管理表单
    ManageBuyPrice,  ///< 管理表单：改买入价
    ManageSellPrice, ///< 管理表单：改卖出价
    ManageAmount,    ///< 管理表单：改每份件数
    ManageRefreshItemName, ///< 管理表单：按箱子里第一格重写牌面第 4 行（商品名）
    ToggleClosed,    ///< 管理表单：停业 / 恢复营业（03）
};

struct ShopFormButton {
    Text           label;
    ShopFormAction action = ShopFormAction::Buy;
};

/// 按钮列表：管理入口只看 `canManage`，买卖按钮只看 `role`、`closed` 与 `unavailableReason`。
///
/// 两件事**故意分开**（spec D5）：
/// - `canManage` 为真时**总是**给管理入口，与 `closed` / `unavailableReason` 无关——否则管理员店一停业
///   就一个按钮都没有，永远无法恢复营业（管理员店的 OP 在交易上是 `Visitor`）；
/// - 买卖按钮仍然按 `role`、`closed` 与 `unavailableReason` 给：停业或店主收不了钱时一个买卖按钮都不给，
///   这不是「不行就不行」，而是提前把必然失败的路径藏起来（DESIGN §8.5）。
///
/// 店主（`Manager`）不跟自己交易：除了管理入口不给买卖按钮（DESIGN §10.1）。
///
/// 文案取自 DESIGN §10.1：`购买（1 份 N 个 / B 价）`、`出售（1 份 N 个 / S 价）`、`管理商店`。
std::vector<ShopFormButton> shopFormButtons(ShopInfoData const& data, ShopFormRole role, bool canManage);

} // namespace ss::shop
