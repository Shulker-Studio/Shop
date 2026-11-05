#pragma once

#include "core/shop/ShopFormRole.h"
#include "core/shop/ShopInfoData.h"
#include "core/sign/SignGeometry.h"

class Player;

namespace ss::shop {

/// 打开商店信息表单。
///
/// 内容与按钮文案全部来自 `data`，这里只做渲染与按钮接线。
/// `sign` 是这块牌的位置：按钮回调里不能拿着旧快照过日子，成交/改价时要用它重新解析商店。
/// `role` 只决定买卖这一侧的视角；管理入口看 `canManage`（`canManageShop(shopActorOf(...))`）——
/// 管理员店在交易上是访客，OP 仍要能进管理表单（spec D5）。
/// 调用方必须先确认「这块牌是合法商店且附着容器可用」（`inspectShop` 返回了结果）。
void openShopForm(Player& viewer, Cell const& sign, ShopInfoData const& data, ShopFormRole role, bool canManage);

} // namespace ss::shop
