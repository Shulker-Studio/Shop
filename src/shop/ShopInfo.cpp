#include "shop/ShopInfo.h"

namespace ss::shop {
namespace {

/// `$ 10` / `金币 10` / 符号为空时只留 `10`。
std::string moneyText(std::string const& symbol, std::int64_t amount) {
    if (symbol.empty()) {
        return std::to_string(amount);
    }
    return symbol + " " + std::to_string(amount);
}

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
    lines.reserve(7);

    lines.push_back("店主：" + ownerText(data));
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
    // 纯收购店（只写 S）的限额是玩家手里的数量与店主资金，不是箱子里的库存（DESIGN §10.3）。
    if (!data.buyPrice) {
        lines.push_back("可成交份数：—（本店只收购，以你手里的数量为准）");
    } else {
        lines.push_back("可成交份数：" + std::to_string(data.stockUnits) + " 份");
    }

    if (data.balance) {
        lines.push_back("余额：" + moneyText(data.currencySymbol, *data.balance));
    } else {
        lines.push_back("余额：暂不可用（经济后端待接入）");
    }

    return lines;
}

std::string shopFormContent(ShopInfoData const& data) {
    auto const  lines = shopInfoLines(data);
    std::string content;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (i != 0) {
            content.push_back('\n');
        }
        content.append(lines[i]);
    }
    return content;
}

std::vector<ShopFormButton> shopFormButtons(ShopInfoData const& data, ShopFormRole role) {
    if (role == ShopFormRole::Manager) {
        // 店主不跟自己交易：只给管理入口（DESIGN §10.1）
        return {
            ShopFormButton{"管理商店", ShopFormAction::Manage}
        };
    }

    std::vector<ShopFormButton> buttons;
    buttons.reserve(2);
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
