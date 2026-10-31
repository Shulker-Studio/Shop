#include "mod/ShopForm.h"

#include "mod/TradeService.h"
#include "shop/BuyForm.h"
#include "shop/TradePricing.h"

#include "ll/api/form/CustomForm.h"
#include "ll/api/form/SimpleForm.h"

#include "mc/world/actor/player/Player.h"

#include <string>
#include <variant>

namespace ss::shop {
namespace {

/// 表单里两个控件的名字。读回来时用它们取值，写错就是静默拿不到值，所以只写一次。
constexpr std::string_view kUnitChoiceField  = "units";
constexpr std::string_view kCustomUnitsField = "custom_units";

/// 按钮按下之后干什么：买入弹份数表单，其余只发「还没实现」。
///
/// 一个 switch 收口所有动作：加动作时编译器会提醒这里漏了一个。
void onButton(Player& player, Cell const& sign, ShopFormAction action) {
    switch (action) {
    case ShopFormAction::Buy:
        requestBuyForm(player, sign);
        return;
    case ShopFormAction::Sell:
        player.sendMessage("出售功能还没实现");
        return;
    case ShopFormAction::Manage:
        player.sendMessage("商店管理功能待实现");
        return;
    }
}

/// 下拉框的回调结果读成下标；读不到就当「没选」（交给 `resolveRequestedUnits` 报错）。
int dropdownIndex(ll::form::CustomFormResult const& result) {
    if (!result) {
        return -1;
    }
    auto it = result->find(std::string{kUnitChoiceField});
    if (it == result->end()) {
        return -1;
    }
    if (auto const* index = std::get_if<uint64>(&it->second)) {
        return static_cast<int>(*index);
    }
    return -1;
}

/// 输入框的回调结果读成文本；读不到就是空串。
std::string inputText(ll::form::CustomFormResult const& result) {
    if (!result) {
        return {};
    }
    auto it = result->find(std::string{kCustomUnitsField});
    if (it == result->end()) {
        return {};
    }
    if (auto const* text = std::get_if<std::string>(&it->second)) {
        return *text;
    }
    return {};
}

} // namespace

void openShopForm(Player& viewer, Cell const& sign, ShopInfoData const& data, ShopFormRole role) {
    ll::form::SimpleForm form{shopFormTitle(data), shopFormContent(data)};

    for (auto const& button : shopFormButtons(data, role)) {
        // 按钮回调可能在非主线程触发：回调里不碰方块与容器。
        // 买入把工作交给 `requestBuyForm`（切回服务端线程后再弹份数表单）；
        // 其余按钮本期只发消息。
        form.appendButton(button.label, [action = button.action, sign](Player& player) {
            onButton(player, sign, action);
        });
    }

    form.sendTo(viewer);
}

void openBuyForm(Player& viewer, Cell const& sign, ShopInfoData const& data, int maxUnits) {
    auto const model = makeBuyFormModel(maxUnits);

    ll::form::CustomForm form{shopFormTitle(data)};
    form.appendLabel(buyFormContent(data, model));
    form.appendDropdown(
        std::string{kUnitChoiceField},
        "买几份",
        buyFormOptions(model),
        /*defaultVal=*/0
    );
    form.appendInput(std::string{kCustomUnitsField}, "自定义份数（选「自定义份数」时才看这里）", "1");

    form.sendTo(viewer, [sign, model, data](Player& player, ll::form::CustomFormResult const& result, auto) {
        // 玩家选了「取消」：什么都不做，别发一条莫名其妙的提示。
        if (!result) {
            return;
        }
        auto const requested = resolveRequestedUnits(model, dropdownIndex(result), inputText(result));
        if (!requested.units) {
            player.sendMessage(requested.error);
            return;
        }
        // 把「表单上看到的金额」一起交上去：两屏之间箱子里的货或价格变了的话，
        // `buyUnits` 会先返回确认表单，而不是按新价直接扣款。
        std::optional<std::int64_t> expectedTotal;
        if (data.buyPrice) {
            expectedTotal = totalPrice(*data.buyPrice, *requested.units);
        }
        // 「共 N 个」也一起交上去：店主改了一份的件数时，只有件数会变。
        int const expectedItems = *requested.units * data.amountPerUnit;
        submitBuy(player, sign, BuySubmission{*requested.units, expectedItems, expectedTotal, /*confirmed=*/false});
    });
}

void openBuyConfirmForm(Player& viewer, Cell const& sign, BuyPlan const& plan, std::string const& content) {
    ll::form::SimpleForm form{"确认交易", content};

    form.appendButton("确认购买（" + std::to_string(plan.units) + " 份）", [sign, plan](Player& player) {
        // 已经看过真实份数与金额了：带着它们再确认一次，后续若又变就按实际成交并报告。
        submitBuy(
            player,
            sign,
            BuySubmission{plan.units, plan.items, std::optional<std::int64_t>{plan.total}, /*confirmed=*/true}
        );
    });
    form.appendButton("取消", [](Player&) {});

    form.sendTo(viewer);
}

} // namespace ss::shop
