#include "mod/ShopForm.h"

#include "mod/TradeService.h"
#include "shop/TradePricing.h"
#include "shop/UnitsForm.h"

#include "ll/api/form/CustomForm.h"
#include "ll/api/form/SimpleForm.h"

#include "mc/world/actor/player/Player.h"

#include <string>
#include <variant>

namespace ss::shop {
namespace {

/// 表单里两个控件的名字。读回来时用它们取值，写错就是静默拿不到值，所以只写一次。
/// 买卖两张份数表单共用这两个名字：同一时刻只会有一张开着。
constexpr std::string_view kUnitChoiceField  = "units";
constexpr std::string_view kCustomUnitsField = "custom_units";

/// 按钮按下之后干什么：买卖各自弹份数表单，管理入口等 06。
///
/// 一个 switch 收口所有动作：加动作时编译器会提醒这里漏了一个。
void onButton(Player& player, Cell const& sign, ShopFormAction action) {
    switch (action) {
    case ShopFormAction::Buy:
        requestBuyForm(player, sign);
        return;
    case ShopFormAction::Sell:
        requestSellForm(player, sign);
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

/// 把表单提交交给对应的方向。买卖两条路只差这一处，别为它各写一整张表单。
void submit(Player& player, Cell const& sign, TradeDirection direction, TradeSubmission const& submission) {
    if (direction == TradeDirection::Sell) {
        submitSell(player, sign, submission);
        return;
    }
    submitBuy(player, sign, submission);
}

/// 表单上显示的那个金额（买入是扣款、卖出是收入）。价格缺失时没有可对照的数。
std::optional<std::int64_t> visibleTotal(ShopInfoData const& data, TradeDirection direction, int units) {
    auto const price = unitPriceOf(data, direction);
    if (!price) {
        return std::nullopt;
    }
    return totalPrice(*price, units);
}

} // namespace

void openShopForm(Player& viewer, Cell const& sign, ShopInfoData const& data, ShopFormRole role) {
    ll::form::SimpleForm form{shopFormTitle(data), shopFormContent(data)};

    for (auto const& button : shopFormButtons(data, role)) {
        // 按钮回调可能在非主线程触发：回调里不碰方块与容器。
        // 买卖把工作交给 `requestBuyForm` / `requestSellForm`（切回服务端线程后再弹份数表单）；
        // 管理入口本期只发消息。
        form.appendButton(button.label, [action = button.action, sign](Player& player) {
            onButton(player, sign, action);
        });
    }

    form.sendTo(viewer);
}

void openTradeForm(Player& viewer, Cell const& sign, TradeDirection direction, ShopInfoData const& data, int maxUnits) {
    auto const model = makeUnitsFormModel(maxUnits);

    ll::form::CustomForm form{shopFormTitle(data)};
    form.appendLabel(unitsFormContent(direction, data, model));
    form.appendDropdown(
        std::string{kUnitChoiceField},
        std::string{directionVerb(direction)} + "几份",
        unitsFormOptions(model),
        /*defaultVal=*/0
    );
    form.appendInput(std::string{kCustomUnitsField}, "自定义份数（选「自定义份数」时才看这里）", "1");

    form.sendTo(viewer, [sign, model, data, direction](Player& player, ll::form::CustomFormResult const& result, auto) {
        // 玩家选了「取消」：什么都不做，别发一条莫名其妙的提示。
        if (!result) {
            return;
        }
        auto const requested = resolveRequestedUnits(model, dropdownIndex(result), inputText(result));
        if (!requested.units) {
            player.sendMessage(requested.error);
            return;
        }
        // 把「表单上看到的金额」与「共 N 个」一起交上去：两屏之间箱子里的货、店主改价或
        // 改份量都会让它们过期，`buyUnits` / `sellUnits` 会先返回确认表单，
        // 而不是按新价直接成交。
        int const expectedItems = *requested.units * data.amountPerUnit;
        submit(
            player,
            sign,
            direction,
            TradeSubmission{
                *requested.units,
                expectedItems,
                visibleTotal(data, direction, *requested.units),
                /*confirmed=*/false
            }
        );
    });
}

void openTradeConfirmForm(
    Player&            viewer,
    Cell const&        sign,
    TradeDirection     direction,
    TradePlan const&   plan,
    std::string const& content
) {
    ll::form::SimpleForm form{"确认交易", content};

    auto const verb = "确认" + std::string{directionVerb(direction)} + "（";
    form.appendButton(verb + std::to_string(plan.units) + " 份）", [sign, plan, direction](Player& player) {
        // 已经看过真实份数与金额了：带着它们再确认一次，后续若又变就按实际成交并报告。
        submit(
            player,
            sign,
            direction,
            TradeSubmission{
                plan.units,
                plan.items,
                std::optional<std::int64_t>{plan.total},
                /*confirmed=*/true
            }
        );
    });
    form.appendButton("取消", [](Player&) {});

    form.sendTo(viewer);
}

} // namespace ss::shop
