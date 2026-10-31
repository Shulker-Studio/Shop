#include "shop/UnitsForm.h"

#include "shop/TradePricing.h"

#include <algorithm>
#include <array>
#include <cstdint>

namespace ss::shop {
namespace {

/// 预设档位：DESIGN §10.1 第 3 步的 `1 份 / 2 份 / 4 份`。
constexpr std::array<int, 3> kPresetUnits{1, 2, 4};

std::string_view trim(std::string_view text) {
    auto const first = text.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) {
        return {};
    }
    auto const last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

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

/// 「单词」：价格是哪一边的。买卖两边的正文只有这些词与两行数字不一样。
struct Wording {
    char const* priceLabel;    ///< `买入价` / `卖出价`
    char const* goodsLabel;    ///< `箱子里的货还够` / `你手里有`
    char const* capLabel;      ///< `这次最多买` / `这次最多卖`
    char const* capHint;       ///< 上限受什么限制
    char const* perChoiceVerb; ///< 每档金额的动词：`/` 后面直接跟金额（买入）或 `得`（卖出）
    char const* totalLabel;    ///< `总价` / `收入`
    char const* moneyLine;     ///< `实际扣款` / `实际收款`
};

/// 买卖两份说法（DESIGN §10.1 / §10.3 的文案）。
///
/// 只放**方向性**的几个词：份数、件数、金额的算法完全一样，不在这里分叉。
Wording wordingOf(TradeDirection direction) {
    if (direction == TradeDirection::Sell) {
        return {
            "卖出价",
            "你手里有",
            "这次最多卖",
            "（受店主资金、箱子空间与单次上限限制）",
            "得 ",
            "收入",
            "实际收款",
        };
    }
    return {
        "买入价",
        "箱子里的货还够",
        "这次最多买",
        "（受余额、背包与单次上限限制）",
        "",
        "总价",
        "实际扣款",
    };
}

/// 一份（`units == 1`）的金额文本；价格缺失或溢出时给占位。
std::string unitPriceText(ShopInfoData const& data, TradeDirection direction) {
    auto const price = direction == TradeDirection::Sell ? data.sellPrice : data.buyPrice;
    if (!price) {
        return "—";
    }
    return moneyText(data.currencySymbol, *price);
}

/// 某一档的金额文本。
std::string totalPriceText(ShopInfoData const& data, TradeDirection direction, int units) {
    auto const price = direction == TradeDirection::Sell ? data.sellPrice : data.buyPrice;
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

std::string unitsFormContent(TradeDirection direction, ShopInfoData const& data, UnitsFormModel const& model) {
    Wording const words = wordingOf(direction);

    std::string content;
    auto const  append = [&content](std::string const& line) { appendLine(content, line); };

    append("商品：" + (data.itemName.empty() ? std::string{"—"} : data.itemName));
    append("每份：" + std::to_string(data.amountPerUnit) + " 个");
    append(std::string{words.priceLabel} + "：" + unitPriceText(data, direction) + " / 份");
    // 卖出的限制是玩家自己手里有几份，买入的限制是商店箱里还有几份（DESIGN §10.3）。
    append(
        std::string{words.goodsLabel} + "："
        + std::to_string(direction == TradeDirection::Sell ? data.heldUnits : data.stockUnits) + " 份"
    );
    if (data.balance) {
        append("余额：" + moneyText(data.currencySymbol, *data.balance));
    }
    append(std::string{words.capLabel} + "：" + std::to_string(model.maxUnits) + " 份" + words.capHint);

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
            + " 个 / " + words.perChoiceVerb + totalPriceText(data, direction, choice.units)
        );
    }
    return content;
}

std::string
unitsConfirmContent(TradeDirection direction, ShopInfoData const& data, TradePlan const& plan, ChangeReason reason) {
    Wording const words = wordingOf(direction);

    std::string content;
    auto const  append = [&content](std::string const& line) { appendLine(content, line); };

    switch (reason) {
    case ChangeReason::Units:
        append("这家店现在只够成交 " + std::to_string(plan.units) + " 份");
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
        append("当前余额：" + moneyText(data.currencySymbol, *data.balance));
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

    auto const text = trim(customText);
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
