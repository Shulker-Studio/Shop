#pragma once

#include "shop/ShopInfo.h"

class Player;

namespace ss::shop {

/// 打开商店信息表单。
///
/// 内容与按钮文案全部来自 `data`（纯逻辑），这里只做渲染与按钮接线。
/// 调用方必须先确认「这块牌是合法商店且附着容器可用」（`inspectShop` 返回了快照）。
void openShopForm(Player& viewer, ShopInfoData const& data, ShopFormRole role);

} // namespace ss::shop
