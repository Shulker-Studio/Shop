#include "mod/ShopForm.h"

#include "ll/api/form/SimpleForm.h"

#include "mc/world/actor/player/Player.h"

namespace ss::shop {
namespace {

/// 本期按钮只说话，不做事：交易在 04/05 接上，商店管理随后。
void notifyPending(Player& player, ShopFormAction action) {
    switch (action) {
    case ShopFormAction::Buy:
        player.sendMessage("交易功能待实现（04：买一份）");
        return;
    case ShopFormAction::Sell:
        player.sendMessage("交易功能待实现（05：卖一份）");
        return;
    case ShopFormAction::Manage:
        player.sendMessage("商店管理功能待实现");
        return;
    }
}

} // namespace

void openShopForm(Player& viewer, ShopInfoData const& data, ShopFormRole role) {
    ll::form::SimpleForm form{shopFormTitle(data), shopFormContent(data)};

    for (auto const& button : shopFormButtons(data, role)) {
        // 按钮回调可能在非主线程触发：这里只发消息，不碰方块与容器。
        form.appendButton(button.label, [action = button.action](Player& player) { notifyPending(player, action); });
    }

    form.sendTo(viewer);
}

} // namespace ss::shop
