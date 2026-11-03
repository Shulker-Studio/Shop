#include "shop/ManageForm.h"
#include "shop/Text.h"

#include "shop/Text.h"

#include <charconv>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace ss::shop {
namespace {

constexpr std::string_view kInputHint = "shop.form.manage.hint";

constexpr std::string_view kBothEmptyReason =
    "shop.form.manage.need_one_price";

/// 输入框里那个价格：只认「空」或「一个非负整数」。
struct PriceInput {
    /// 输入框里写了东西（去空白后非空）。`false` = 留空 = 这一侧不做生意。
    bool given = false;
    /// `given` 且能解析成一非负整数时才有值；`given && !price` 就是「输入不合法」。
    std::optional<std::int64_t> price;
};

/// 改哪一侧的价格。两侧的解析与校验一模一样，只有提示里的名字与落到哪个字段不同。
enum class PriceSide { Buy, Sell };

constexpr std::string_view priceLabelOf(PriceSide side) { return side == PriceSide::Buy ? "shop.form.buy_price" : "shop.form.sell_price"; }

/// 某一侧价格写错了。两侧共用这句话，只有名字不同。
Text notAnIntegerReason(PriceSide side) {
    return text::keyed("shop.manage.price_not_integer", Tone::Default, {
        text::keyed(std::string{priceLabelOf(side)})
    });
}

/// 空串（含只有空白）= 没填。
///
/// `from_chars` 必须把整串吃完：`1.5` / `1K` / `1 0` 都会停在中间，一律算非法而不是取前缀。
/// 负数它能解析出来，所以要单独拒绝。`int64` 溢出返回 `result_out_of_range`，同样算非法。
PriceInput parsePriceInput(std::string_view text) {
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

/// 价格改动的收尾：改完的定义能不能写进牌面。
///
/// 两侧都空 → 第 3 行没有 `B`/`S` → `looksLikeShopSign` 为假 → 牌与箱子整体失去保护（spec D6）；
/// 卖出价高于买入价 → 与手写编辑同一条规则（`sellPriceHigherThanBuy`）。
/// 只有两个价格函数用它：改件数造不出这两种状态（件数一个字段都不碰价）。
ManageEdit validatedEdit(ShopSpec next, ShopSignConfig const& config) {
    if (!next.buyPrice && !next.sellPrice) {
        return {.spec = std::nullopt, .error = text::keyed(std::string{kBothEmptyReason})};
    }
    if (sellPriceHigherThanBuy(next.buyPrice, next.sellPrice, config)) {
        return {.spec = std::nullopt, .error = sellPriceAboveBuyMessage(next.buyPrice, next.sellPrice)};
    }
    return {.spec = std::move(next), .error = {}};
}

/// `applyManageEdit` 里的一格价格：没填（`nullopt`）= 这次不改这一侧，跳过；
/// 填了就解析并写进 `next`。返回空 = 接着干；有字符串 = 直接交给玩家的原因（输入不合法）。
std::optional<Text> applyPrice(
    std::optional<std::string> const& input,
    PriceSide                         side,
    ShopSpec&                         next
) {
    if (!input) {
        return std::nullopt;
    }
    auto const parsed = parsePriceInput(*input);
    if (parsed.given && !parsed.price) {
        return notAnIntegerReason(side);
    }
    (side == PriceSide::Buy ? next.buyPrice : next.sellPrice) = parsed.price;
    return std::nullopt;
}

/// 管理表单上的一个按钮：只写标题。
///
/// 以前标题后面跟一句 `（现在 $ 10 / 份）`，但顶层正文刚刚列过买入价与卖出价——
/// 同一个数字在按钮上再说一遍，读的人还得比对两次才能确定是同一个。
Text priceButtonLabel(std::string_view title) {
    return text::keyed(std::string{title}, Tone::Default);
}

} // namespace

std::vector<Text> shopManageFormContent(ShopInfoData const& data, bool waxed) {
    auto lines = shopFormContent(data);
    // 管理表单在商店信息下面接一段「怎么改」，用同一条分隔线切开，别让提示混进价格里。
    lines.push_back(text::section(text::keyed("shop.form.section.manage", Tone::Value)));
    if (waxed) {
        // 蜡不可逆（DESIGN §9.2）：不写这句，店主会以为改价彻底没路了。
        lines.push_back(text::muted(text::keyed("shop.form.manage.waxed")));
    }
    lines.push_back(text::muted(text::keyed(std::string{kInputHint})));
    return lines;
}

std::vector<ShopFormButton> shopManageFormButtons(ShopInfoData const& data) {
    std::vector<ShopFormButton> buttons{
        {priceButtonLabel("shop.manage.button.buy_price"), ShopFormAction::ManageBuyPrice},
        {priceButtonLabel("shop.manage.button.sell_price"), ShopFormAction::ManageSellPrice},
        {priceButtonLabel("shop.manage.button.amount"), ShopFormAction::ManageAmount},
    };

    // 「更新商品名」：牌面第 4 行只是商品名字的**快照**，权威是箱子里第一个非空槽位
    // （DESIGN §5.3）。店主（或管理员店的 OP）换掉箱子里的东西之后，那行字会一直是旧的，
    // 路人读到的是已经不卖的东西。箱子空着时不给这个按钮：那时它只会把牌面原文抄一遍。
    if (data.hasProduct) {
        buttons.push_back({text::keyed("shop.manage.button.refresh_item_name", Tone::Default),
                           ShopFormAction::ManageRefreshItemName});
    }

    // 停业与恢复营业是同一个动作的两个方向，按当前状态只给一个：两个按钮同时挂着，
    // 店主得自己先想明白哪个才算话。
    buttons.push_back({data.closed ? text::keyed("shop.manage.button.reopen", Tone::Default)
                                   : text::keyed("shop.manage.button.close", Tone::Default),
                       ShopFormAction::ToggleClosed});
    return buttons;
}

ManageEdit applyManageEdit(ShopSpec const& current, ManageEditRequest const& request, ShopSignConfig const& config) {
    ShopSpec next = current;

    if (auto const error = applyPrice(request.buyPrice, PriceSide::Buy, next)) {
        return {.spec = std::nullopt, .error = *error};
    }
    if (auto const error = applyPrice(request.sellPrice, PriceSide::Sell, next)) {
        return {.spec = std::nullopt, .error = *error};
    }

    if (request.amount) {
        // 件数的文法（只收 ASCII 十进制、落在 1..maxShopAmount）只有一份，就是牌面第 2 行那个
        // `parseAmountLine`：表单只是允许输入框两端带空白（与买卖价一致），拿走之后的字串没区别。
        // 手写编辑那条路不接受空白，因为它读的是牌面：“牌面第 2 行是权威整数”（DESIGN §5.1）。
        //
        // 与价格那边唯一的宽严差异：`parseAmountLine` 会先剥颜色码，所以 `§a16` 在这里能认出成 16，
        // 而价格框会把它当非法。牌面上颜色码是常态，不该反过来让店主在输入框里卡死。
        auto const amount = parseAmountLine(trimCopy(*request.amount), config.maxShopAmount);
        if (!amount) {
            return {.spec = std::nullopt, .error = amountOutOfRangeMessage(config.maxShopAmount)};
        }
        next.amount = *amount;
    }

    if (request.closed) {
        next.closed = *request.closed;
    }

    // 收尾校验对「只改件数 / 只停业」是空跑：它们造不出「两侧都空」或「S > B」，
    // 而现在的牌已经过了 `parseShopSignText`。集中在一处的好处是：以后再加一格时，
    // 不会因为它看起来「碰不到价」就把校验漏掉。
    return validatedEdit(std::move(next), config);
}

ManageEdit withBuyPrice(ShopSpec const& current, std::string_view input, ShopSignConfig const& config) {
    return applyManageEdit(current, {.buyPrice = std::string{input}}, config);
}

ManageEdit withSellPrice(ShopSpec const& current, std::string_view input, ShopSignConfig const& config) {
    return applyManageEdit(current, {.sellPrice = std::string{input}}, config);
}

ManageEdit withAmount(ShopSpec const& current, std::string_view input, ShopSignConfig const& config) {
    return applyManageEdit(current, {.amount = std::string{input}}, config);
}

ManageEdit withToggledClosed(ShopSpec const& current, std::string_view /*input*/, ShopSignConfig const& config) {
    // 停业不需要输入：点一下就是「切成另一个状态」。翻转读的是当前牌面解析出的值，
    // 所以两个店主同时点也不会各自基于一个旧值写回去。
    return applyManageEdit(current, {.closed = !current.closed}, config);
}

} // namespace ss::shop
