#include "shop/UnitsForm.h"

#include "shop/Text.h"
#include "shop/TradePricing.h"

#include <algorithm>
#include <array>
#include <cstdint>

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
    char const* capHint;           ///< 上限受什么限制
    char const* totalLabel;        ///< `总价` / `收入`
    char const* moneyLine;         ///< `实际扣款` / `实际收款`
    char const* unitsShortMessage; ///< 份数变少时的第一句
};

Wording const kBuyWording{
    "买入价",
    "商店箱里的货还够",
    "这次最多买",
    "（受余额、背包与单次上限限制）",
    "总价",
    "实际扣款",
    "这家店现在只够成交 ",
};

Wording const kSellWording{
    "卖出价",
    "你手里有",
    "这次最多卖",
    "（受店主余额、商店箱空间与单次上限限制）",
    "收入",
    "实际收款",
    "这一笔现在只成交得了 ",
};

Wording const& wordingOf(TradeDirection direction) {
    return direction == TradeDirection::Sell ? kSellWording : kBuyWording;
}

/// 上限受什么限制。卖出侧的默认那句提到「店主余额」，而管理员店没有这一条（资金侧不设上限，
/// DESIGN §7.4），所以按店的性质换一句——不能把不存在的上限摆给玩家看。
std::string capHintOf(ShopInfoData const& data, TradeDirection direction) {
    if (direction == TradeDirection::Sell && data.adminShop) {
        return "（受你手里的货、商店箱空间与单次上限限制）";
    }
    return wordingOf(direction).capHint;
}

/// 剩下的货有几份：买入看商店箱（`stockUnits`，管理员店是无限）、卖出看玩家手里（`heldUnits`，永远有限）。
StockView goodsOf(ShopInfoData const& data, TradeDirection direction) {
    return direction == TradeDirection::Sell ? StockView::makeFinite(data.heldUnits) : data.stockUnits;
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
            model.choices.push_back({preset, std::to_string(preset) + " 份"});
        }
    }
    model.choices.push_back({0, "自定义份数"});
    return model;
}

std::vector<std::string> unitsFormOptions(UnitsFormModel const& model) {
    std::vector<std::string> options;
    options.reserve(model.choices.size());
    for (auto const& choice : model.choices) {
        options.push_back(choice.label);
    }
    return options;
}

int choiceIndexForOption(UnitsFormModel const& model, std::string_view optionText) {
    for (std::size_t i = 0; i < model.choices.size(); ++i) {
        if (model.choices[i].label == optionText) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

std::string unitsFormContent(TradeDirection direction, ShopInfoData const& data, UnitsFormModel const& model) {
    Wording const& words = wordingOf(direction);

    std::string content;
    auto const  append = [&content](std::string const& line) { appendLine(content, line); };

    append("商品：" + (data.itemName.empty() ? std::string{"—"} : data.itemName));
    append("每份：" + std::to_string(data.amountPerUnit) + " 个");
    append(std::string{words.priceLabel} + "：" + unitPriceText(data, direction) + " / 份");
    // 卖出的限额是玩家自己手里有几份，买入的限额是商店箱里还有几份（DESIGN §10.3）。
    append(std::string{words.goodsLabel} + "：" + stockUnitsText(goodsOf(data, direction)));
    if (direction == TradeDirection::Sell) {
        // 卖出那三个上限中另两个（§10.3）：箱子还能装多少、店主付得起多少。
        // 买入侧的对应两行是「商店箱里的货还够」与「余额」。
        append("商店箱还能装：" + std::to_string(data.boxRoomUnits) + " 份");
        if (data.ownerBalance) {
            append("店主余额：" + moneyText(data.currencySymbol, *data.ownerBalance));
        }
    }
    if (data.balance) {
        // 这是玩家自己的余额（两个方向都是）：卖出时真正的限额来自店主余额，
        // 所以标签写清「你的」，与上限那行的「店主余额」分开。
        append("你的余额：" + moneyText(data.currencySymbol, *data.balance));
    }
    append(std::string{words.capLabel} + "：" + std::to_string(model.maxUnits) + " 份" + capHintOf(data, direction));

    // 每一档都把总件数与总金额写出来：选哪一档，看到的数就是实际成交的数。
    for (auto const& choice : model.choices) {
        if (choice.units == 0) {
            // 自定义档没法实时算，至少把乘数与上限写全，玩家自己就能算出来。
            append(
                "自定义份数：" + std::string{words.totalLabel} + " = " + unitPriceText(data, direction)
                + " × 份数（1 到 " + std::to_string(model.maxUnits) + " 份，恒为整数）"
            );
            continue;
        }
        append(
            "选 " + std::to_string(choice.units) + " 份 → 共 " + std::to_string(choice.units * data.amountPerUnit)
            + " 个 / " + totalPriceText(data, direction, choice.units)
        );
    }
    return content;
}

std::string
unitsConfirmContent(TradeDirection direction, ShopInfoData const& data, TradePlan const& plan, ChangeReason reason) {
    Wording const& words = wordingOf(direction);

    std::string content;
    auto const  append = [&content](std::string const& line) { appendLine(content, line); };

    switch (reason) {
    case ChangeReason::Units:
        // 份数变少的成因在两个方向上不一样：买入多半是商店箱不够，卖出多半是
        // 自己手里的货、店主的钱或商店箱空间不够，所以第一句要分开说。
        append(std::string{words.unitsShortMessage} + std::to_string(plan.units) + " 份");
        break;
    case ChangeReason::Items:
        append("这家店「一份的件数」变了，现在 " + std::to_string(data.amountPerUnit) + " 个算一份");
        break;
    case ChangeReason::Price:
        append(std::string{"这家店的"} + words.priceLabel + "刚刚变了");
        break;
    }

    // 三样都写全：玩家要能一眼看出这次到底给多少、收（付）多少。
    append("成交份数：" + std::to_string(plan.units) + " 份");
    append("总件数：" + std::to_string(plan.items) + " 个");
    append(std::string{words.moneyLine} + "：" + moneyText(data.currencySymbol, plan.total));
    if (data.balance) {
        append("你的余额：" + moneyText(data.currencySymbol, *data.balance));
    }
    append("确认后按上面的份数与金额成交。");
    return content;
}

RequestedUnits resolveRequestedUnits(UnitsFormModel const& model, int choiceIndex, std::string_view customText) {
    if (choiceIndex < 0 || choiceIndex >= static_cast<int>(model.choices.size())) {
        return {.error = "份数选择不对"};
    }

    int const chosen = model.choices[static_cast<std::size_t>(choiceIndex)].units;
    if (chosen > 0) {
        return {.units = chosen};
    }

    auto const text = trimView(customText);
    if (text.empty()) {
        return {.error = "选了「自定义份数」，请在输入框里填一个整数"};
    }
    auto const units = parseUnits(text);
    if (!units) {
        return {.error = "自定义份数要写一个正整数"};
    }
    if (*units < 1 || *units > model.maxUnits) {
        return {.error = "自定义份数要在 1 到 " + std::to_string(model.maxUnits) + " 之间"};
    }
    return {.units = *units};
}

} // namespace ss::shop
