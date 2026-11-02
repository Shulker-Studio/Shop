#include "shop/ShopInfo.h"
#include "shop/Text.h"

namespace ss::shop {
namespace {

/// 「N 份」或「无限」。
///
/// 无限**不编数字**：编出来的那个数会被玩家当成「还剩这么多」，而这正是票 01/02 要拆掉的
/// 两套真相。也**不解释为什么无限**——「无限」两个字说完了，再补一句「管理员店永远有货」
/// 只是把同一件事说第二遍。
Text countUnitsText(StockView const& stock) {
    if (auto const finite = stock.finiteUnits()) {
        return text::keyed("shop.form.units_count", Tone::Default, {text::lit(std::to_string(*finite))});
    }
    return text::keyed("shop.form.unlimited");
}

Text ownerText(ShopInfoData const& data) {
    if (data.adminShop) {
        return text::keyed("shop.form.title.admin");
    }
    if (data.ownerName.empty()) {
        return text::keyed("shop.form.unknown_owner_short");
    }
    return text::lit(data.ownerName);
}

} // namespace

std::string moneyText(std::string const& symbol, std::int64_t amount) {
    if (symbol.empty()) {
        return std::to_string(amount);
    }
    return symbol + " " + std::to_string(amount);
}

Text stockUnitsText(StockView const& stock) {
    return countUnitsText(stock);
}

Text boxRoomText(StockView const& stock) {
    return countUnitsText(stock);
}

std::optional<std::int64_t> unitPriceOf(ShopInfoData const& data, TradeDirection direction) {
    return direction == TradeDirection::Sell ? data.sellPrice : data.buyPrice;
}

void appendLine(std::string& content, std::string const& line) {
    if (!content.empty()) {
        content.push_back('\n');
    }
    content.append(line);
}

Text ownerOfflineReason() { return text::keyed("shop.form.reason.owner_offline"); }

Text ownerFundsUnknownReason() { return text::keyed("shop.form.reason.owner_funds_unknown"); }

Text playerFundsUnknownReason() { return text::keyed("shop.form.reason.player_funds_unknown"); }

Text noEconomyReason() { return text::keyed("shop.form.reason.no_economy"); }

Text noSystemAccountReason() { return text::keyed("shop.form.reason.no_system_account"); }

Text emptyBoxReason() { return text::keyed("shop.form.reason.empty_box"); }

Text unavailableReasonForEmptyBox(bool adminShop) {
    if (!adminShop) {
        return emptyBoxReason();
    }
    // 管理员店的模板箱空着不是「卖光了」：它永远不会卖光，只是还没告诉它卖什么。
    // 写成缺货会让玩家一直在那儿等补货，而补货这件事对模板箱没有意义。
    return text::keyed("shop.form.reason.admin_no_sample");
}

Text taxChargeFailureReason(Text const& detail) {
    return text::keyed("shop.form.reason.tax_failed", Tone::Default, {detail});
}

Text taxBrokenReason(Text const& detail) {
    return text::keyed("shop.form.reason.tax_broken", Tone::Default, {detail});
}

Text shopFormTitle(ShopInfoData const& data) {
    if (data.adminShop) {
        return text::keyed("shop.form.title.admin", Tone::Title);
    }
    if (data.ownerName.empty()) {
        return text::keyed("shop.form.title.unknown_owner", Tone::Title);
    }
    return text::keyed("shop.form.title.owner", Tone::Title, {text::lit(data.ownerName)});
}

namespace {

/// 「$ 10 / 份」。
Text pricePerUnitText(ShopInfoData const& data, std::int64_t price) {
    // 价格是这一屏最该被一眼看到的东西：整行金（`/ 份` 跟着继承），与白色的份量、绿色的余额分开。
    return text::keyed("shop.form.price_per_unit", Tone::Default, {
        text::lit(moneyText(data.currencySymbol, price), Tone::Warn)
    });
}

std::vector<Text> infoLinesOf(ShopInfoData const& data) {
    std::vector<Text> lines;
    lines.reserve(8);

    lines.push_back(text::field(text::keyed("shop.form.owner"), ownerText(data)));
    // 停业写在最上面：玩家点开表单第一眼就该看到这家店不接客，
    // 而不必先读完价格再自己推。与「状态：<原因>」不同：那个是后端/在线状态，这个是店主拉的闸。
    if (data.closed) {
        lines.push_back(text::heading(text::keyed("shop.form.closed")));
    }
    lines.push_back(text::field(
        text::keyed("shop.form.item"),
        data.itemName.empty() ? text::keyed("shop.form.none") : text::lit(data.itemName)
    ));
    lines.push_back(text::field(
        text::keyed("shop.form.per_unit"),
        text::keyed("shop.form.per_unit_value", Tone::Default, {
            text::lit(std::to_string(data.amountPerUnit))
        })
    ));

    // 一条灰线切开三段：这家店是什么 / 卖什么价 / 还能不能成交。
    lines.push_back(text::section(text::keyed("shop.form.section.price", Tone::Value)));

    if (data.buyPrice) {
        lines.push_back(text::field(text::keyed("shop.form.buy_price"), pricePerUnitText(data, *data.buyPrice)));
    } else {
        lines.push_back(text::field(text::keyed("shop.form.buy_price"), text::keyed("shop.form.not_sold")));
    }

    if (data.sellPrice) {
        lines.push_back(text::field(text::keyed("shop.form.sell_price"), pricePerUnitText(data, *data.sellPrice)));
    } else {
        lines.push_back(text::field(text::keyed("shop.form.sell_price"), text::keyed("shop.form.not_bought")));
    }

    lines.push_back(text::section(text::keyed("shop.form.section.stock", Tone::Value)));

    // 只有卖得出去的店（写了买入价）才有「可成交份数」这个说法；
    // 纯收购店（只写 S）的限额是玩家手里的数量与店主资金，不是商店箱里的货（DESIGN §10.3）。
    if (!data.buyPrice) {
        lines.push_back(text::field(text::keyed("shop.form.stock_units"), text::keyed("shop.form.stock_sell_only")));
    } else {
        lines.push_back(text::field(text::keyed("shop.form.stock_units"), stockUnitsText(data.stockUnits)));
    }

    if (data.balance) {
        lines.push_back(text::field(
            text::keyed("shop.form.balance"),
            text::lit(moneyText(data.currencySymbol, *data.balance), Tone::Good)
        ));
    } else {
        // 空有两个原因（没有可用后端、读不到这名玩家的账户），不猜是哪一个：
        // 不能交易时正文下面还会多一行「状态：<具体原因>」（DESIGN §8.2）。
        lines.push_back(text::field(text::keyed("shop.form.balance"), text::keyed("shop.form.balance_unreadable")));
    }

    // 不能交易就直说，别让玩家点进去才被拒（DESIGN §8.5）。
    if (!text::empty(data.unavailableReason)) {
        // 不能交易是这一屏唯一的坏消息：整行红，别混在白色值里被读漏。
        auto reason = data.unavailableReason;
        if (reason.tone == Tone::Default) {
            reason.tone = Tone::Bad;
        }
        lines.push_back(text::field(text::keyed("shop.form.status"), std::move(reason)));
    }

    return lines;
}

} // namespace

std::vector<Text> shopInfoLines(ShopInfoData const& data) { return infoLinesOf(data); }

std::vector<Text> shopFormContent(ShopInfoData const& data) { return infoLinesOf(data); }

ShopFormRole shopFormRoleOf(ShopActor actor, bool adminShop) {
    // 管理员店的管理权只给 OP（§11），但那不等于「OP 不能买东西」：它的对手方是系统账户（§7.4），
    // 店主不在这里。
    if (adminShop) {
        return ShopFormRole::Visitor;
    }
    return canManageShop(actor) ? ShopFormRole::Manager : ShopFormRole::Visitor;
}

std::vector<ShopFormButton> shopFormButtons(ShopInfoData const& data, ShopFormRole role, bool canManage) {
    std::vector<ShopFormButton> buttons;

    // 管理入口先放：它对店主与 OP 恒存在，与「现在能不能交易」无关（spec D5）。
    if (canManage) {
        buttons.push_back({text::keyed("shop.form.button.manage", Tone::Default), ShopFormAction::Manage});
    }

    // 店主不跟自己交易：管理入口之外没有买卖按钮（DESIGN §10.1）
    if (role == ShopFormRole::Manager) {
        return buttons;
    }

    // 店不能交易：停业与「原因非空」一样都不摆买卖按钮（停业时正文写「这家店暂停营业」，
    // 其它原因在 `shopInfoLines` 的「状态：」那一行）。两条合成一个分支，免得两个逐字相同的 return。
    if (data.closed || !text::empty(data.unavailableReason)) {
        // 必然失败的路径不摆在按钮上（DESIGN §8.5）；正文已经写了原因。
        return buttons;
    }

    // 按钮只写动作，**不重复正文已有的价格与份数**：正文就在按钮上方，再写一遍
    // `购买（1 份 64 个 / $ 10）` 只是把同样的三个数字念第二遍，还把按钮撑得老长。
    // 按钮文字也**一个颜色码都不发**（`Tone::Default`）：按钮底有浅灰（未选中）与深绿
    // （悬停）两种状态，任何固定颜色都只能在其中一种里可读。让客户端用它自己的主题色。
    if (data.buyPrice) {
        buttons.push_back({text::keyed("shop.form.button.buy", Tone::Default), ShopFormAction::Buy});
    }
    if (data.sellPrice) {
        buttons.push_back({text::keyed("shop.form.button.sell", Tone::Default), ShopFormAction::Sell});
    }
    return buttons;
}

} // namespace ss::shop
