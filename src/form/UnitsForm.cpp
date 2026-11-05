#include "form/UnitsForm.h"
#include "util/i18n/Text.h"

#include "util/i18n/Text.h"
#include "core/trade/TradePricing.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>

namespace ss::shop {
namespace {

/// 预设档位：DESIGN §10.1 第 3 步的 `1 份 / 2 份 / 4 份`。
constexpr std::array<int, 3> kPresetUnits{1, 2, 4};

/// 只接受 ASCII 十进制。最多 9 位，所以不可能溢出 `int`。
std::optional<int> parseUnits(std::string_view text) {
    if (text.empty() || text.size() > 9) {
        return std::nullopt;
    }
    std::int64_t value = 0;
    for (char const ch : text) {
        if (ch < '0' || ch > '9') {
            return std::nullopt;
        }
        value = value * 10 + (ch - '0');
    }
    return static_cast<int>(value);
}

/// 「方向性」的几个词：买卖两边的正文只在这里分叉，份数、件数、金额的算法完全一样。
struct Wording {
    char const* priceLabel;        ///< `买入价` / `卖出价`
    char const* goodsLabel;        ///< `商店箱里的货还够` / `你手里有`
    char const* capLabel;          ///< `这次最多买` / `这次最多卖`
    char const* totalLabel;        ///< `总价` / `收入`
    char const* moneyLine;         ///< `实际扣款` / `实际收款`
    char const* unitsShortMessage; ///< 份数变少时的第一句
};

Wording const kBuyWording{
    "shop.form.buy_price",
    "shop.form.chest_has",
    "shop.form.max_buy",
    "shop.form.total",
    "shop.form.charged",
    "shop.form.units_reduced.buy",
};

Wording const kSellWording{
    "shop.form.sell_price",
    "shop.form.you_have",
    "shop.form.max_sell",
    "shop.form.income",
    "shop.form.received",
    "shop.form.units_reduced.sell",
};

Wording const& wordingOf(TradeDirection direction) {
    return direction == TradeDirection::Sell ? kSellWording : kBuyWording;
}

/// 剩下的货有几份：买入看商店箱（`stockUnits`，管理员店是无限）、卖出看玩家手里（`heldUnits`，永远有限）。
UnitCount goodsOf(ShopInfoData const& data, TradeDirection direction) {
    return direction == TradeDirection::Sell ? UnitCount::makeFinite(data.heldUnits) : data.stockUnits;
}

/// 「一份」的金额文本；价格缺失时给占位。
std::string unitPriceText(ShopInfoData const& data, TradeDirection direction) {
    auto const price = unitPriceOf(data, direction);
    if (!price) {
        return "—";
    }
    return moneyText(data.currencySymbol, *price);
}

/// 某一档的金额文本。
std::string totalPriceText(ShopInfoData const& data, TradeDirection direction, int units) {
    auto const price = unitPriceOf(data, direction);
    if (!price) {
        return "—";
    }
    auto const total = totalPrice(*price, units);
    if (!total) {
        return "—";
    }
    return moneyText(data.currencySymbol, *total);
}

} // namespace

UnitsFormModel makeUnitsFormModel(int maxUnits) {
    UnitsFormModel model;
    // 不把 0 夹成 1：那样会摆出一个必然失败的「1 份」档。上限为 0 时只留自定义，
    // 调用方本来就该在弹出表单之前就拒掉。
    model.maxUnits = std::max(0, maxUnits);

    for (int const preset : kPresetUnits) {
        if (preset <= model.maxUnits) {
            model.choices.push_back(
                {preset,
                 text::keyed("shop.form.units_count", Tone::Default, {text::lit(std::to_string(preset))})}
            );
        }
    }
    model.choices.push_back({0, text::keyed("shop.form.custom_units")});
    return model;
}

std::vector<std::string> unitsFormOptions(UnitsFormModel const& model, std::string_view localeCode) {
    std::vector<std::string> options;
    options.reserve(model.choices.size());
    for (auto const& choice : model.choices) {
        options.push_back(text::render(choice.label, localeCode));
    }
    return options;
}

int choiceIndexForOption(UnitsFormModel const& model, std::string_view optionText, std::string_view localeCode) {
    for (std::size_t i = 0; i < model.choices.size(); ++i) {
        if (text::render(model.choices[i].label, localeCode) == optionText) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int itemsForUnits(int units, int amountPerUnit) {
    auto const items = static_cast<std::int64_t>(units) * static_cast<std::int64_t>(amountPerUnit);
    return static_cast<int>(std::min<std::int64_t>(items, std::numeric_limits<int>::max()));
}

namespace {

/// 「N 份」。
Text unitsCountText(int units) {
    return text::keyed("shop.form.units_count", Tone::Default, {text::lit(std::to_string(units))});
}

/// 「N 个」。
Text itemsCountText(int items) {
    return text::keyed("shop.form.per_unit_value", Tone::Default, {text::lit(std::to_string(items))});
}

/// 「$ 10 / 份」。
Text pricePerUnitText(ShopInfoData const& data, TradeDirection direction) {
    return text::keyed("shop.form.price_per_unit", Tone::Default, {
        text::lit(unitPriceText(data, direction))
    });
}

Text balanceText(ShopInfoData const& data, std::int64_t amount) {
    return text::lit(moneyText(data.currencySymbol, amount));
}

} // namespace

std::vector<Text> unitsFormContent(
    TradeDirection        direction,
    ShopInfoData const&   data,
    UnitsFormModel const& model
) {
    Wording const& words = wordingOf(direction);

    std::vector<Text> lines;
    lines.reserve(10);

    lines.push_back(text::field(
        text::keyed("shop.form.item"),
        data.itemName.empty() ? text::keyed("shop.form.none") : text::lit(data.itemName)
    ));
    lines.push_back(text::field(
        text::keyed("shop.form.per_unit"),
        itemsCountText(data.amountPerUnit)
    ));
    lines.push_back(text::field(text::keyed(words.priceLabel), pricePerUnitText(data, direction)));
    // 卖出的限额是玩家自己手里有几份，买入的限额是商店箱里还有几份（DESIGN §10.3）。
    lines.push_back(text::field(text::keyed(words.goodsLabel), stockUnitsText(goodsOf(data, direction))));
    if (direction == TradeDirection::Sell) {
        // 卖出那三个上限中另两个（§10.3）：箱子还能装多少、店主付得起多少。
        // 买入侧的对应两行是「商店箱里的货还够」与「余额」。
        lines.push_back(text::field(text::keyed("shop.form.chest_room"), boxRoomText(data.boxRoomUnits)));
        if (data.ownerBalance) {
            lines.push_back(text::field(text::keyed("shop.form.owner_balance"), balanceText(data, *data.ownerBalance)));
        }
    }
    if (data.balance) {
        // 这是玩家自己的余额（两个方向都是）：卖出时真正的限额来自店主余额，
        // 所以标签写清「你的」，与上限那行的「店主余额」分开。
        lines.push_back(text::field(text::keyed("shop.form.your_balance"), balanceText(data, *data.balance)));
    }
    // 只写「这次最多买： 3 份」。
    // 以前后面还挂一句「（受余额、背包与单次上限限制）」——「最多」本身就是在说上限，
    // 括号只是把同一件事解释一遍；想知道是哪条卡住的，上面几行已经逐项列出来了。
    lines.push_back(text::field(text::keyed(words.capLabel), unitsCountText(model.maxUnits)));

    // 每一档都把总件数与总金额写出来：选哪一档，看到的数就是实际成交的数。
    for (auto const& choice : model.choices) {
        if (choice.units == 0) {
            // 自定义档没法实时算，至少把乘数与上限写全，玩家自己就能算出来。
            lines.push_back(text::field(
                text::keyed("shop.form.custom_units"),
                text::keyed("shop.form.custom_units_line", Tone::Default, {
                    text::keyed(words.totalLabel),
                    text::lit(unitPriceText(data, direction)),
                    text::lit(std::to_string(model.maxUnits)),
                })
            ));
            continue;
        }
        lines.push_back(text::joined(
            text::keyed("shop.form.choice_label", Tone::Default, {text::lit(std::to_string(choice.units))}),
            text::keyed("shop.form.choice_value", Tone::Default, {
                text::lit(std::to_string(itemsForUnits(choice.units, data.amountPerUnit))),
                text::lit(totalPriceText(data, direction, choice.units)),
            })
        ));
    }
    return lines;
}

std::vector<Text> unitsConfirmContent(
    TradeDirection      direction,
    ShopInfoData const& data,
    TradePlan const&    plan,
    ChangeReason        reason
) {
    Wording const& words = wordingOf(direction);

    std::vector<Text> lines;
    lines.reserve(6);

    switch (reason) {
    case ChangeReason::Units:
        // 份数变少的成因在两个方向上不一样：买入多半是商店箱不够，卖出多半是
        // 自己手里的货、店主的钱或商店箱空间不够，所以第一句要分开说。
        lines.push_back(text::heading(text::keyed(
            words.unitsShortMessage,
            Tone::Default,
            {text::lit(std::to_string(plan.units))}
        )));
        break;
    case ChangeReason::Items:
        lines.push_back(text::heading(text::keyed(
            "shop.form.changed.items",
            Tone::Default,
            {text::lit(std::to_string(data.amountPerUnit))}
        )));
        break;
    case ChangeReason::Price:
        lines.push_back(text::heading(text::keyed(
            "shop.form.changed.price",
            Tone::Default,
            {text::keyed(words.priceLabel)}
        )));
        break;
    }

    // 三样都写全：玩家要能一眼看出这次到底给多少、收（付）多少。
    lines.push_back(text::field(text::keyed("shop.form.deal_units"), unitsCountText(plan.units)));
    lines.push_back(text::field(text::keyed("shop.form.deal_items"), itemsCountText(plan.items)));
    lines.push_back(text::field(
        text::keyed(words.moneyLine),
        text::lit(moneyText(data.currencySymbol, plan.total))
    ));
    if (data.balance) {
        lines.push_back(text::field(text::keyed("shop.form.your_balance"), balanceText(data, *data.balance)));
    }
    lines.push_back(text::muted(text::keyed("shop.form.confirm_note")));
    return lines;
}

RequestedUnits resolveRequestedUnits(UnitsFormModel const& model, int choiceIndex, std::string_view customText) {
    if (choiceIndex < 0 || choiceIndex >= static_cast<int>(model.choices.size())) {
        return {.error = text::keyed("shop.form.units_invalid")};
    }

    int const chosen = model.choices[static_cast<std::size_t>(choiceIndex)].units;
    if (chosen > 0) {
        return {.units = chosen};
    }

    auto const text = trimView(customText);
    if (text.empty()) {
        return {.error = text::keyed("shop.form.custom_units_missing")};
    }
    auto const units = parseUnits(text);
    if (!units) {
        return {.error = text::keyed("shop.form.custom_units_positive")};
    }
    if (*units < 1 || *units > model.maxUnits) {
        return {.error = text::keyed(
            "shop.form.custom_units_range",
            Tone::Default,
            {text::lit(std::to_string(model.maxUnits))}
        )};
    }
    return {.units = *units};
}

} // namespace ss::shop
