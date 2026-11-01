#include "shop/ManageForm.h"

#include "shop/Text.h"

#include <charconv>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace ss::shop {
namespace {

constexpr std::string_view kBuyPriceHint = "改价只写数字：留空表示这一侧不做生意，至少要留一侧价格";

constexpr std::string_view kNotAnIntegerReason = "买入价只能写非负整数（例如 10），或者留空表示这一侧不做生意";

constexpr std::string_view kBothEmptyReason = "至少要留一侧价格，否则这块牌就不再是商店牌（箱子会失去保护）";

/// 输入框里那个价格：只认「空」或「一个非负整数」。
struct BuyPriceInput {
    /// 输入框里写了东西（去空白后非空）。`false` = 留空 = 这一侧不做生意。
    bool given = false;
    /// `given` 且能解析成一非负整数时才有值；`given && !price` 就是「输入不合法」。
    std::optional<std::int64_t> price;
};

/// 空串（含只有空白）= 没填。
///
/// `from_chars` 必须把整串吃完：`1.5` / `1K` / `1 0` 都会停在中间，一律算非法而不是取前缀。
/// 负数它能解析出来，所以要单独拒绝。`int64` 溢出返回 `result_out_of_range`，同样算非法。
BuyPriceInput parseBuyPrice(std::string_view text) {
    const std::string_view trimmed = trimView(text);
    if (trimmed.empty()) {
        return {};
    }
    std::int64_t value      = 0;
    auto const [end, error] = std::from_chars(trimmed.data(), trimmed.data() + trimmed.size(), value);
    if (error != std::errc{} || end != trimmed.data() + trimmed.size() || value < 0) {
        return {.given = true, .price = std::nullopt};
    }
    return {.given = true, .price = value};
}

} // namespace

std::string shopManageFormContent(ShopInfoData const& data, bool waxed) {
    std::string content = shopFormContent(data);
    if (waxed) {
        // 蜡不可逆（DESIGN §9.2）：不写这句，店主会以为改价彻底没路了。
        appendLine(content, "这块牌上了蜡：手写编辑仍被锁，这里可以直接改");
    }
    appendLine(content, std::string{kBuyPriceHint});
    return content;
}

std::vector<ShopFormButton> shopManageFormButtons(ShopInfoData const& data) {
    std::string label  = "改买入价（现在";
    label             += data.buyPrice ? moneyText(data.currencySymbol, *data.buyPrice) + " / 份" : "不出售";
    label             += "）";
    return {
        ShopFormButton{std::move(label), ShopFormAction::ManageBuyPrice}
    };
}

ManageEdit withBuyPrice(ShopSpec const& current, std::string_view input, ShopSignConfig const& config) {
    auto const parsed = parseBuyPrice(input);
    if (parsed.given && !parsed.price) {
        return {.spec = std::nullopt, .error = std::string{kNotAnIntegerReason}};
    }

    ShopSpec next = current;
    next.buyPrice = parsed.price;

    if (!next.buyPrice && !next.sellPrice) {
        return {.spec = std::nullopt, .error = std::string{kBothEmptyReason}};
    }
    if (sellPriceHigherThanBuy(next.buyPrice, next.sellPrice, config)) {
        return {.spec = std::nullopt, .error = sellPriceAboveBuyMessage(next.buyPrice, next.sellPrice)};
    }
    return {.spec = std::move(next), .error = {}};
}

} // namespace ss::shop
