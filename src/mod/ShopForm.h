#pragma once

#include "shop/ContainerFinder.h"
#include "shop/ShopInfo.h"
#include "shop/TradePlan.h"

#include <string>

class Player;

namespace ss::shop {

/// 打开商店信息表单。
///
/// 内容与按钮文案全部来自 `data`（纯逻辑），这里只做渲染与按钮接线。
/// `sign` 是这块牌的位置：按钮回调里不能拿着旧快照过日子，成交时要用它重新解析商店。
/// 调用方必须先确认「这块牌是合法商店且附着容器可用」（`inspectShop` 返回了结果）。
void openShopForm(Player& viewer, Cell const& sign, ShopInfoData const& data, ShopFormRole role);

/// 打开「几份」表单（DESIGN §10.1 第 3 步）。买卖共用同一张表单：档位与上限都是「份数」的事。
///
/// `maxUnits` 由调用方算好（货的份数、收货方容量、付款方余额三者取最小）；这里只负责
/// 把它变成下拉框与正文，并把玩家选的份数交给 `submitBuy` / `submitSell`。
/// 表单值不是权威数据：提交时会按最新状态再算一遍（DESIGN §10.4）。
void openTradeForm(Player& viewer, Cell const& sign, TradeDirection direction, ShopInfoData const& data, int maxUnits);

/// 部分成交或改价的确认表单（DESIGN §10.3）。买卖共用，只有按钮与文案的方向不同。
///
/// `plan` 是 `buyUnits` / `sellUnits` 重算后的真实份数、件数与金额，`content` 是它的正文：
/// 玩家点「确认」才真正成交，点「取消」则什么都不动。确认后仍会按最新状态再算一次，
/// 若又变了就按实际成交并如实报告。
void openTradeConfirmForm(
    Player&            viewer,
    Cell const&        sign,
    TradeDirection     direction,
    TradePlan const&   plan,
    std::string const& content
);

} // namespace ss::shop
