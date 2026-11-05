#include "form/InfoForm.h"

#include "form/ActionDispatch.h"
#include "form/FormModel.h"
#include "util/i18n/Locale.h"
#include "util/i18n/Text.h"

#include "ll/api/form/SimpleForm.h"

#include "mc/world/actor/player/Player.h"

namespace ss::shop {

void openShopForm(Player& viewer, Cell const& sign, ShopInfoData const& data, ShopFormRole role, bool canManage) {
    auto const localeCode = viewer.getLocaleCode();
    ll::form::SimpleForm form{
        text::render(shopFormTitle(data), localeCode),
        text::renderLines(shopFormContent(data), localeCode)
    };

    for (auto const& button : shopFormButtons(data, role, canManage)) {
        // 按钮回调可能在非主线程触发：回调里不碰方块与容器。
        // 买卖把工作交给 `requestBuyForm` / `requestSellForm`，管理入口交给 `requestManageForm`
        // （两者都会先切回服务端线程）。
        form.appendButton(text::render(button.label, localeCode), [action = button.action, sign](Player& player) {
            onButton(player, sign, action);
        });
    }

    form.sendTo(viewer);
}

} // namespace ss::shop
