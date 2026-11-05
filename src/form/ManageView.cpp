#include "form/ManageView.h"

#include "core/container/ContainerFinder.h"
#include "core/sign/ShopSign.h"
#include "core/sign/SignProtection.h"
#include "core/shop/ShopInspector.h"
#include "core/shop/SignOwner.h"
#include "event/BlockResync.h"
#include "form/ActionDispatch.h"
#include "form/FormSupport.h"
#include "form/ManageForm.h"
#include "util/config/Config.h"
#include "util/i18n/Locale.h"
#include "util/i18n/Message.h"
#include "util/i18n/Reason.h"
#include "util/i18n/Text.h"

#include "Shop.h"

#include "ll/api/form/CustomForm.h"
#include "ll/api/form/SimpleForm.h"

#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/actor/SignBlockActor.h"
#include "mc/world/level/block/actor/SignTextSide.h"

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace ss::shop {
namespace {

struct ManageTarget {
    std::optional<ShopSnapshot> shop;
    bool                        waxed = false;
    Text                        message;
};

ManageTarget inspectManageTarget(BlockSource& region, BlockPos const& pos, Player& player, ShopConfig const& config) {
    auto const interaction = inspectShop(region, pos, player, config);
    if (!interaction) {
        return {.message = shopInvalidMessage()}; // 这一格现在已经不是商店牌
    }
    if (!interaction->shop) {
        return {
            .message = interaction->unavailable == ShopUnavailable::NoContainer ? shopBoxMissingMessage()
                                                                                : shopInvalidMessage()
        };
    }
    if (!canManageShop(interaction->actor)) {
        return {.message = ownerOnlyEditMessage(interaction->shop->info.ownerName, interaction->shop->info.adminShop)};
    }
    return {.shop = interaction->shop, .waxed = interaction->waxed};
}

/// 「改一项」里与表单长什么样无关的两件接线：怎么把输入合成为新定义，写完之后跟玩家说什么。
///
/// 提交时只需要这两样，不需要 title/placeholder 那些字符串——所以单独一个类型。
using ManageEditFn  = ManageEdit (*)(ShopSpec const&, std::string_view, ShopSignConfig const&);
using EditMessageFn = std::string (*)(ShopSpec const&, ShopConfig const&);

/// 写牌时第 4 行（商品名）从哪儿来。
///
/// 商品名的权威是**箱子里第一个非空槽位**（DESIGN §5.3），牌面那行只是快照。改价、改件数、停业
/// 一律沿用牌面原文（店主没要求动它）；「更新商品名」反过来，专门按现在的商品重写这一行。
enum class ItemLineSource : std::uint8_t {
    SignText,     ///< 沿用牌面第 4 行
    ChestProduct, ///< 用商店箱里第一个非空槽位的展示名
};

struct ManageChange {
    ManageEditFn   apply;          ///< 把输入框原文合成为新的商店定义
    EditMessageFn  successMessage; ///< 写完之后跟玩家说的那句
    ItemLineSource itemLine = ItemLineSource::SignText; ///< 第 4 行从哪儿来
};

/// 买卖两侧的提示只差名字与「清空」的说法，句子只有这一份。
std::string priceEditMessage(
    std::string_view            title,
    std::optional<std::int64_t> price,
    std::string_view            clearedWord,
    ShopConfig const&           config
) {
    if (!price) {
        return text::render(text::keyed("shop.form.price_cleared", ss::shop::Tone::Default, {
            text::keyed(std::string{title}),
            text::keyed(std::string{clearedWord}),
        }));
    }
    return text::render(text::keyed("shop.form.price_changed", ss::shop::Tone::Default, {
        text::keyed(std::string{title}),
        text::lit(moneyText(config.economy.currencySymbol, *price)),
    }));
}

std::string buyPriceEditMessage(ShopSpec const& spec, ShopConfig const& config) {
    return priceEditMessage("shop.form.buy_price", spec.buyPrice, "shop.form.side_off.sell", config);
}

std::string sellPriceEditMessage(ShopSpec const& spec, ShopConfig const& config) {
    return priceEditMessage("shop.form.sell_price", spec.sellPrice, "shop.form.side_off.buy", config);
}

std::string amountEditMessage(ShopSpec const& spec, ShopConfig const&) {
    return text::render(text::keyed(
        "shop.form.amount_changed",
        ss::shop::Tone::Default,
        {text::lit(std::to_string(spec.amount))}
    ));
}

/// 「更新商品名」不改定义：价格、件数、停业位原样返回，只有第 4 行换成箱子里的商品
/// （由 `submitManageEdit` 用 `ItemLineSource::ChestProduct` 接管）。
ManageEdit withRefreshedItemName(ShopSpec const& current, std::string_view, ShopSignConfig const&) {
    return ManageEdit{current};
}

std::string refreshedItemNameMessage(ShopSpec const&, ShopConfig const&) {
    // 不复述商品名：牌面就在玩家眼前，第二遍名字只会让这一行更长。
    return ui::msg("shop.form.refresh_item_name.done");
}

std::string closedEditMessage(ShopSpec const& spec, ShopConfig const&) {
    // 停业不是关店：牌子、库存、归属、保护都还在，所以提示里说清「随时能开回来」。
    return ui::plain(text::keyed(spec.closed ? "shop.form.closed_edit" : "shop.form.reopened_edit"));
}

/// 管理表单上一张单输入框表单的全部内容：控件名、三句文案与初值，再加上校验与成功提示。
///
/// 三张表单（改买入价 / 改卖出价 / 改件数）只差这些东西，所以只写一份布线，按 `action` 取一份。
struct ManageInputField {
    std::string  control;      ///< 回调里用哪个名字读这个控件（写错就是静默拿不到值）
    std::string  titleSuffix;  ///< 表单标题后缀，三张表单只差这一处
    std::string  label;        ///< 输入框标签：说清单位与范围
    std::string  placeholder;  ///< 输入框空着时的提示
    std::string  defaultValue; ///< 初值 = 当前值；空 = 这一侧现在不做生意
    ManageChange change;
};

std::string currentValueOf(std::optional<std::int64_t> price) { return price ? std::to_string(*price) : std::string{}; }

/// `action` 必须是三个「改一项」的动作；其他动作返回空（调用方不该传）。
std::optional<ManageInputField>
manageInputField(ShopFormAction action, ShopInfoData const& data, ShopSignConfig const& signConfig) {
    switch (action) {
    case ShopFormAction::ManageBuyPrice:
        return ManageInputField{
            .control      = std::string{kBuyPriceField},
            .titleSuffix  = "shop.form.input.buy_price.title",
            .label        = "shop.form.input.buy_price.label",
            .placeholder  = data.buyPrice ? currentValueOf(data.buyPrice)
                                          : ui::plain(text::keyed("shop.form.input.side_off.sell")),
            .defaultValue = currentValueOf(data.buyPrice),
            .change       = {&withBuyPrice, &buyPriceEditMessage},
        };
    case ShopFormAction::ManageSellPrice:
        return ManageInputField{
            .control      = std::string{kSellPriceField},
            .titleSuffix  = "shop.form.input.sell_price.title",
            .label        = "shop.form.input.sell_price.label",
            .placeholder  = data.sellPrice ? currentValueOf(data.sellPrice)
                                           : ui::plain(text::keyed("shop.form.input.side_off.buy")),
            .defaultValue = currentValueOf(data.sellPrice),
            .change       = {&withSellPrice, &sellPriceEditMessage},
        };
    case ShopFormAction::ManageAmount:
        return ManageInputField{
            .control      = std::string{kAmountField},
            .titleSuffix  = "shop.form.input.amount.title",
            .label        = ui::plain(text::keyed(
                "shop.form.input.amount.label",
                ss::shop::Tone::Default,
                {text::lit(std::to_string(signConfig.maxShopAmount))}
            )),
            .placeholder  = ui::plain(text::keyed(
                "shop.form.input.amount.placeholder",
                ss::shop::Tone::Default,
                {text::lit(std::to_string(signConfig.maxShopAmount))}
            )),
            .defaultValue = std::to_string(data.amountPerUnit),
            .change       = {&withAmount, &amountEditMessage},
        };
    case ShopFormAction::Buy:
    case ShopFormAction::Sell:
    case ShopFormAction::Manage:
    case ShopFormAction::ToggleClosed:
    case ShopFormAction::ManageRefreshItemName:
        break;
    }
    return std::nullopt;
}

/// 管理表单的提交：重新解析牌面、合成新文本、写牌、把这一格发给整维度。
///
/// 三个入口共用这一段（票面要求：不许各写一份格式化、各写一次发包）；它们只传进来自己的
/// `ManageChange`（合成函数 + 成功提示），表单文案一概不进来。
/// 提交时**从牌面重新读一遍**，不用打开表单时缓存的 `ShopInfoData`：表单开着的时候
/// 牌可能已经被别人改过（DESIGN §10.4 的同一条口径）。
void submitManageEdit(Player& player, Cell const& sign, ManageChange change, std::string input) {
    onSignServerThread(player, [sign, change, input = std::move(input)](Player& who, ShopConfig const& config) {
        auto&          region = who.getDimensionBlockSource();
        BlockPos const pos    = toBlockPos(sign);

        auto* signActor = signBlockActorAt(region, pos);
        if (signActor == nullptr) {
            ui::send(who, ui::Tone::Failure, kSignGoneMessage);
            return;
        }

        auto const target = inspectManageTarget(region, pos, who, config);
        if (!target.shop) {
            ui::send(who, ui::Tone::Failure, target.message);
            return;
        }

        // 「更新商品名」要有商品可抄：箱子空着时它只会把牌面原文抄回牌上，不如直说。
        if (change.itemLine == ItemLineSource::ChestProduct && !target.shop->info.hasProduct) {
            ui::send(who, ui::Tone::Failure, "shop.form.refresh_item_name.no_product");
            return;
        }

        auto const signConfig = toSignConfig(config);
        auto const text       = signActor->getMessage(SignTextSide::Front);
        // `editorIsOp` 固定 `true`：它只影响「是不是管理员店」，与此刻是谁在改无关
        // （`parseShopSignFront` 同一条理由）。解析出的 `adminShop` 也不参与出文本
        // （`formatShopSign` 不看它）；真正决定写牌 XUID 的是 `inspectShop` 读出的 `info.adminShop`。
        auto const parsed = parseShopSignText(text, /*editorIsOp=*/true, signConfig);
        if (!parsed.ok()) {
            ui::send(who, ui::Tone::Failure, shopInvalidMessage());
            return;
        }

        auto const edit = change.apply(*parsed.spec, input, signConfig);
        if (!edit.ok()) {
            // 失败只提示，牌面一字不改（spec D6）。
            ui::send(who, ui::Tone::Failure, edit.error);
            return;
        }

        // 规范化回写：第 1 行（店主名）沿用牌面原文——店主名不该被一次改价动到。
        // 第 4 行（商品名）按 `change.itemLine` 取：改价那几个动作原样带过去，
        // 「更新商品名」取箱子里现在的商品（`inspectManageTarget` 刚读过，`itemName` 就是它）。
        auto const  lines = splitSignText(text);
        auto const& itemLine =
            change.itemLine == ItemLineSource::ChestProduct ? target.shop->info.itemName : lines[SignLine::ItemLine];
        auto const formatted = formatShopSign(*edit.spec, lines[SignLine::OwnerLine], itemLine);

        // 管理员店的归属靠「第 1 行 == adminShopName + 编辑者是 OP」，不靠 XUID
        // （流水里管理员店的 `owner.xuid` 也是空串）。
        std::string const ownerId = target.shop->info.adminShop ? std::string{} : target.shop->ownerXuid;
        signActor->setMessageForServerScripingOnly(SignTextSide::Front, joinSignText(formatted), std::move(ownerId));

        // 这次写入不是玩家发起的编辑，客户端不会自己重画：按 DESIGN §9.2 发给整个维度。
        resyncBlockActorToDimension(region, pos);

        ui::send(who, ui::Tone::Success, change.successMessage(*edit.spec, config));
    });
}

void openManageForm(Player& player, Cell const& sign, ShopInfoData const& data, bool waxed) {
    auto const localeCode = player.getLocaleCode();
    ll::form::SimpleForm form{
        text::render(
            text::sequence({
                shopFormTitle(data),
                text::keyed("shop.form.manage_suffix", ss::shop::Tone::Muted),
            }),
            localeCode
        ),
        text::renderLines(shopManageFormContent(data, waxed), localeCode)
    };

    for (auto const& button : shopManageFormButtons(data)) {
        form.appendButton(
            text::render(button.label, localeCode),
            [action = button.action, sign](Player& who) { onButton(who, sign, action); }
        );
    }

    form.sendTo(player);
}

void openManageInputForm(
    Player&               player,
    Cell const&           sign,
    ShopFormAction        action,
    ShopInfoData const&   data,
    bool                  waxed,
    ShopSignConfig const& signConfig
) {
    auto const input = manageInputField(action, data, signConfig);
    if (!input) {
        // 只有「三个输入框动作」能走到这里。走到了说明表单按钮与动作表脱节了——
        // 这是编程错误，不能表现为「按钮点了没反应」。
        Shop::getInstance().getSelf().getLogger().error(ui::msg("shop.log.form.not_an_input_action"));
        ui::send(player, ui::Tone::Failure, "shop.form.button_unavailable");
        return;
    }

    auto const localeCode = player.getLocaleCode();
    ll::form::CustomForm custom{
        text::render(shopFormTitle(data), localeCode) + text::render(text::keyed(input->titleSuffix), localeCode)
    };
    // 正文与信息表单同源：改价时看得到「箱里还剩几份」「余额够不够」，改完不用再点回来看。
    custom.appendLabel(text::renderLines(shopManageFormContent(data, waxed), localeCode));
    custom.appendInput(input->control, input->label, input->placeholder, input->defaultValue);

    custom.sendTo(
        player,
        [sign,
         change  = input->change,
         control = input->control](Player& who, ll::form::CustomFormResult const& result, auto) {
            // 玩家选了「取消」：什么都不做，别发一条莫名其妙的提示。
            if (!result) {
                return;
            }
            if (auto const value = formInput(result, control)) {
                submitManageEdit(who, sign, change, *value);
                return;
            }
            // 读不到字符串就是表单出问题了：按「输入不合法」处理，不静默当留空。
            // 发包要回到服务端线程：表单回调线程不确定（见 `onSignServerThread`）。
            onSignServerThread(who, [](Player& player, ShopConfig const&) {
                ui::send(player, ui::Tone::Failure, "shop.form.input_unreadable");
            });
        }
    );
}

} // namespace

void requestManageForm(Player& viewer, Cell const& sign) {
    // 管理表单要读牌面与容器（正文里的库存、余额），所以先切回服务端线程。
    onSignServerThread(viewer, [sign](Player& player, ShopConfig const& config) {
        auto&          region = player.getDimensionBlockSource();
        BlockPos const pos    = toBlockPos(sign);

        auto const target = inspectManageTarget(region, pos, player, config);
        if (!target.shop) {
            ui::send(player, ui::Tone::Failure, target.message);
            return;
        }
        openManageForm(player, sign, target.shop->info, target.waxed);
    });
}

void requestManageInputForm(Player& viewer, Cell const& sign, ShopFormAction action) {
    onSignServerThread(viewer, [sign, action](Player& player, ShopConfig const& config) {
        auto&          region = player.getDimensionBlockSource();
        BlockPos const pos    = toBlockPos(sign);

        auto const target = inspectManageTarget(region, pos, player, config);
        if (!target.shop) {
            ui::send(player, ui::Tone::Failure, target.message);
            return;
        }
        openManageInputForm(player, sign, action, target.shop->info, target.waxed, toSignConfig(config));
    });
}

void requestManageToggleClosed(Player& viewer, Cell const& sign) {
    // 不做任何前置检查，也不弹输入框：`submitManageEdit` 自己会重新取牌、重新鉴权、
    // 重新解析牌面，然后才写牌。停业与改价在这里必须是同一条路。
    submitManageEdit(viewer, sign, ManageChange{&withToggledClosed, &closedEditMessage}, std::string{});
}

void requestManageRefreshItemName(Player& viewer, Cell const& sign) {
    // 与停业一样不做前置检查：`submitManageEdit` 会重新取牌、重新鉴权、重新解析，
    // 再读一遍箱子里的商品，然后才写牌。
    submitManageEdit(
        viewer,
        sign,
        ManageChange{&withRefreshedItemName, &refreshedItemNameMessage, ItemLineSource::ChestProduct},
        std::string{}
    );
}

} // namespace ss::shop
