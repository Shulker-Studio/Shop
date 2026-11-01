#include "shop/ShopInfo.h"

namespace ss::shop {
namespace {

std::string ownerText(ShopInfoData const& data) {
    if (data.adminShop) {
        return "管理员商店";
    }
    if (data.ownerName.empty()) {
        return "未知";
    }
    return data.ownerName;
}

} // namespace

std::string moneyText(std::string const& symbol, std::int64_t amount) {
    if (symbol.empty()) {
        return std::to_string(amount);
    }
    return symbol + " " + std::to_string(amount);
}

std::string stockUnitsText(StockView const& stock) {
    auto const finite = stock.finiteUnits();
    if (!finite) {
        // 无限不编数字：编出来的那个数会被玩家当成「还剩这么多」。括号里写清为什么无限，
        // 因为同一家店卖出方向的「货」是有限的（玩家背包），只有模板箱是无限的。
        return "无限（管理员店，永远有货）";
    }
    return std::to_string(*finite) + " 份";
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

std::string ownerOfflineReason() { return "店主不在线，现在不能交易（当前经济后端不支持离线记账）"; }

std::string ownerFundsUnknownReason() { return "读不到店主的余额，现在不能交易（经济后端没给出这个账户的余额）"; }

std::string playerFundsUnknownReason() { return "读不到你的余额，这一笔没法完成（经济后端没给出这个账户的余额）"; }

std::string noEconomyReason() { return "服务器没有可用的经济后端，暂时不能交易"; }

std::string noSystemAccountReason() {
    return "当前经济后端没有系统账户，管理员商店现在不能交易（换用支持它的后端后恢复）";
}

std::string emptyBoxReason() { return "商店箱里没有货，现在没法交易（补货后恢复）"; }

std::string taxChargeFailureReason(std::string const& detail) { return "收取商店税失败（" + detail + "）"; }

std::string taxBrokenReason(std::string const& detail) {
    return "收取商店税时钱已经动过且没能还原（" + detail + "），请联系管理员";
}

std::string shopFormTitle(ShopInfoData const& data) {
    if (data.adminShop) {
        return "管理员商店";
    }
    if (data.ownerName.empty()) {
        return "未知店主的商店";
    }
    return data.ownerName + " 的商店";
}

std::vector<std::string> shopInfoLines(ShopInfoData const& data) {
    std::vector<std::string> lines;
    lines.reserve(8);

    lines.push_back("店主：" + ownerText(data));
    // 停业写在最上面：玩家点开表单第一眼就该看到这家店不接客，
    // 而不必先读完价格再自己推。与「状态：<原因>」不同：那个是后端/在线状态，这个是店主拉的闸。
    if (data.closed) {
        lines.push_back("这家店暂停营业");
    }
    lines.push_back("商品：" + (data.itemName.empty() ? std::string{"—"} : data.itemName));
    lines.push_back("每份：" + std::to_string(data.amountPerUnit) + " 个");

    if (data.buyPrice) {
        lines.push_back("买入价：" + moneyText(data.currencySymbol, *data.buyPrice) + " / 份");
    } else {
        lines.push_back("买入价：不出售");
    }

    if (data.sellPrice) {
        lines.push_back("卖出价：" + moneyText(data.currencySymbol, *data.sellPrice) + " / 份");
    } else {
        lines.push_back("卖出价：不收购");
    }

    // 只有卖得出去的店（写了买入价）才有「可成交份数」这个说法；
    // 纯收购店（只写 S）的限额是玩家手里的数量与店主资金，不是商店箱里的货（DESIGN §10.3）。
    if (!data.buyPrice) {
        lines.push_back("可成交份数：—（本店只收购，以你手里的数量为准）");
    } else {
        lines.push_back("可成交份数：" + stockUnitsText(data.stockUnits));
    }

    if (data.balance) {
        lines.push_back("余额：" + moneyText(data.currencySymbol, *data.balance));
    } else {
        // 空有两个原因（没有可用后端、读不到这名玩家的账户），不猜是哪一个：
        // 不能交易时正文下面还会多一行「状态：<具体原因>」（DESIGN §8.2）。
        lines.push_back("余额：暂时读不到（经济后端没给出这个账户的余额）");
    }

    // 不能交易就直说，别让玩家点进去才被拒（DESIGN §8.5）。
    if (!data.unavailableReason.empty()) {
        lines.push_back("状态：" + data.unavailableReason);
    }

    return lines;
}

std::string shopFormContent(ShopInfoData const& data) {
    std::string content;
    for (auto const& line : shopInfoLines(data)) {
        appendLine(content, line);
    }
    return content;
}

ShopFormRole shopFormRoleOf(ShopActor actor, bool adminShop) {
    // 管理员店的管理权只给 OP（§11），但那不等于「OP 不能买东西」：
    // 它的对手方是系统账户（§7.4），店主不在这里。
    if (adminShop) {
        return ShopFormRole::Visitor;
    }
    return canManageShop(actor) ? ShopFormRole::Manager : ShopFormRole::Visitor;
}

std::vector<ShopFormButton> shopFormButtons(ShopInfoData const& data, ShopFormRole role, bool canManage) {
    std::vector<ShopFormButton> buttons;

    // 管理入口先放：它对店主与 OP 恒存在，与「现在能不能交易」无关（spec D5）。
    if (canManage) {
        buttons.push_back({"管理商店", ShopFormAction::Manage});
    }

    // 店主不跟自己交易：管理入口之外没有买卖按钮（DESIGN §10.1）
    if (role == ShopFormRole::Manager) {
        return buttons;
    }

    // 店不能交易：停业与「原因非空」一样都不摆买卖按钮（停业时正文写「这家店暂停营业」，
    // 其它原因在 `shopInfoLines` 的「状态：」那一行）。两条合成一个分支，免得两个逐字相同的 return。
    if (data.closed || !data.unavailableReason.empty()) {
        // 必然失败的路径不摆在按钮上（DESIGN §8.5）；正文已经写了原因。
        return buttons;
    }

    auto const unitText = "1 份 " + std::to_string(data.amountPerUnit) + " 个";

    if (data.buyPrice) {
        buttons.push_back(
            {"购买（" + unitText + " / " + moneyText(data.currencySymbol, *data.buyPrice) + "）", ShopFormAction::Buy}
        );
    }
    if (data.sellPrice) {
        buttons.push_back(
            {"出售（" + unitText + " / " + moneyText(data.currencySymbol, *data.sellPrice) + "）", ShopFormAction::Sell}
        );
    }
    return buttons;
}

} // namespace ss::shop
