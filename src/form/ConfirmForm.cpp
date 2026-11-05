#include "form/ConfirmForm.h"

#include "core/trade/TradeService.h"
#include "form/UnitsForm.h"
#include "util/i18n/Locale.h"
#include "util/i18n/Message.h"
#include "util/i18n/Text.h"

#include "ll/api/form/SimpleForm.h"

#include "mc/world/actor/player/Player.h"

#include <optional>
#include <string>

namespace ss::shop {

void openTradeConfirmForm(
    Player&            viewer,
    Cell const&        sign,
    TradeDirection     direction,
    TradePlan const&   plan,
    Text const&        content
) {
    auto const localeCode = viewer.getLocaleCode();
    ll::form::SimpleForm form{
        text::render(text::keyed("shop.form.confirm_title", ss::shop::Tone::Title), localeCode),
        text::render(content, localeCode)
    };

    // 按钮文字不上色：按钮底有浅灰与深绿两种状态，固定颜色必在其一里失效。
    auto const verb = text::render(text::keyed(
        direction == TradeDirection::Buy ? "shop.form.confirm_buy" : "shop.form.confirm_sell",
        ss::shop::Tone::Default,
        {text::lit(std::to_string(plan.units))}
    ), localeCode);
    form.appendButton(verb, [sign, plan, direction](Player& player) {
        // 已经看过真实份数与金额了：带着它们再确认一次，后续若又变就按实际成交并报告。
        submitTradeForm(
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
    form.appendButton(text::render(text::keyed("shop.form.cancel", ss::shop::Tone::Default), localeCode), [](Player&) {});

    form.sendTo(viewer);
}

} // namespace ss::shop
