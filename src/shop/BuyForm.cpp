#include "shop/BuyForm.h"

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

std::string itemPriceText(ShopInfoData const& data, int units) {
    if (!data.buyPrice) {
        return "—";
    }
    auto const total = totalPrice(*data.buyPrice, units);
    if (!total) {
        return "—";
    }
    return moneyText(data.currencySymbol, *total);
}

} // namespace

BuyFormModel makeBuyFormModel(int maxUnits) {
    BuyFormModel model;
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

std::vector<std::string> buyFormOptions(BuyFormModel const& model) {
    std::vector<std::string> options;
    options.reserve(model.choices.size());
    for (auto const& choice : model.choices) {
        options.push_back(choice.label);
    }
    return options;
}

std::string buyFormContent(ShopInfoData const& data, BuyFormModel const& model) {
    std::string content;
    auto const  append = [&content](std::string const& line) { appendLine(content, line); };

    append("商品：" + (data.itemName.empty() ? std::string{"—"} : data.itemName));
    append("每份：" + std::to_string(data.amountPerUnit) + " 个");
    if (data.buyPrice) {
        append("买入价：" + moneyText(data.currencySymbol, *data.buyPrice) + " / 份");
    }
    append("箱子里的货还够：" + std::to_string(data.stockUnits) + " 份");
    if (data.balance) {
        append("余额：" + moneyText(data.currencySymbol, *data.balance));
    }
    append("这次最多买：" + std::to_string(model.maxUnits) + " 份（受余额、背包与单次上限限制）");

    // 每一档都把总件数与总价写出来：选哪一档，看到的数就是实际扣的数。
    for (auto const& choice : model.choices) {
        if (choice.units == 0) {
            // 自定义档没法实时算，至少把乘数与上限写全，玩家自己就能算出总价。
            append(
                "自定义份数：总价 = " + itemPriceText(data, 1) + " × 份数（1 到 " + std::to_string(model.maxUnits)
                + " 份，恒为整数）"
            );
            continue;
        }
        append(
            "选 " + std::to_string(choice.units) + " 份 → 共 " + std::to_string(choice.units * data.amountPerUnit)
            + " 个 / " + itemPriceText(data, choice.units)
        );
    }
    return content;
}

std::string buyConfirmContent(ShopInfoData const& data, BuyPlan const& plan, ChangeReason reason) {
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
        append("这家店的买入价刚刚变了");
        break;
    }

    // 三样都写全：玩家要能一眼看出这次到底给多少、扣多少。
    append("成交份数：" + std::to_string(plan.units) + " 份");
    append("总件数：" + std::to_string(plan.items) + " 个");
    append("实际扣款：" + moneyText(data.currencySymbol, plan.total));
    if (data.balance) {
        append("当前余额：" + moneyText(data.currencySymbol, *data.balance));
    }
    append("确认后按上面的份数与金额成交。");
    return content;
}

RequestedUnits resolveRequestedUnits(BuyFormModel const& model, int choiceIndex, std::string_view customText) {
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
