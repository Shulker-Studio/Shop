#pragma once

#include "core/sign/SignGeometry.h"
#include "core/trade/TradePlan.h"
#include "util/i18n/Text.h"

class Player;

namespace ss::shop {

/// 部分成交或改价的确认表单（DESIGN §10.3）。买卖共用，只有按钮与文案的方向不同。
///
/// `plan` 是 `buyUnits` / `sellUnits` 重算后的真实份数、件数与金额，`content` 是它的正文：
/// 玩家点「确认」才真正成交，点「取消」则什么都不动。确认后仍会按最新状态再算一次，
/// 若又变了就按实际成交并如实报告。
void openTradeConfirmForm(
    Player&          viewer,
    Cell const&      sign,
    TradeDirection   direction,
    TradePlan const& plan,
    Text const&      content
);

} // namespace ss::shop
