#include "form/ActionDispatch.h"

#include "core/trade/TradeService.h"
#include "form/ManageView.h"

namespace ss::shop {

/// 按钮按下之后干什么：买卖各自弹份数表单，管理入口进管理表单，管理表单上的按钮进各自的输入框。
///
/// 这里是**动作分发**的唯一一处：加动作时编译器会提醒这里漏了一个。
/// 两张表单共用它：管理表单上的动作不会出现在第一张表单上，但接线是同一条。
/// （动作×文案/校验的那张表不在这里，在 `manageInputField`：一个是分发，一个是数据。）
/// 回调可能在非主线程触发（DESIGN §14），所以这里不碰世界，只把工作交给 `request*`。
void onButton(Player& player, Cell const& sign, ShopFormAction action) {
    switch (action) {
    case ShopFormAction::Buy:
        requestBuyForm(player, sign);
        return;
    case ShopFormAction::Sell:
        requestSellForm(player, sign);
        return;
    case ShopFormAction::Manage:
        requestManageForm(player, sign);
        return;
    case ShopFormAction::ManageBuyPrice:
    case ShopFormAction::ManageSellPrice:
    case ShopFormAction::ManageAmount:
        requestManageInputForm(player, sign, action);
        return;
    case ShopFormAction::ToggleClosed:
        // 停业不需要输入框：直接走提交那一段（与三个输入框共用 `submitManageEdit` 的链）。
        requestManageToggleClosed(player, sign);
        return;
    case ShopFormAction::ManageRefreshItemName:
        // 与停业同构：没有输入框，直接把牌面第 4 行按箱子里的商品重写。
        requestManageRefreshItemName(player, sign);
        return;
    }
}

} // namespace ss::shop
