#include "form/ShopForm.h"
#include "util/i18n/Reason.h"
#include "util/i18n/Locale.h"
#include "util/i18n/Message.h"
#include "util/i18n/Text.h"

#include "event/BlockResync.h"
#include "core/container/ContainerFinder.h"
#include "core/economy/Service.h"
#include "Shop.h"
#include "core/shop/SignOwner.h"
#include "core/shop/ShopInspector.h"
#include "core/trade/TradeService.h"
#include "form/ManageForm.h"
#include "core/sign/SignProtection.h"
#include "core/trade/TradePricing.h"
#include "form/UnitsForm.h"

#include "ll/api/form/CustomForm.h"
#include "ll/api/form/SimpleForm.h"
#include "ll/api/thread/ServerThreadExecutor.h"

#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/actor/SignBlockActor.h"
#include "mc/world/level/block/actor/SignTextSide.h"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace ss::shop {
namespace {

/// 表单里几个控件的名字。读回来时用它们取值，写错就是静默拿不到值，所以只写一次。
/// 买卖两张份数表单共用前两个：同一时刻只会有一张开着。
constexpr std::string_view kUnitChoiceField  = "units";
constexpr std::string_view kCustomUnitsField = "custom_units";
constexpr std::string_view kBuyPriceField    = "buy_price";
constexpr std::string_view kSellPriceField   = "sell_price";
constexpr std::string_view kAmountField      = "amount";

/// 提交时牌子已经不在了（改价与改件数共用这一句，所以说「这次的改动」）。
constexpr std::string_view kSignGoneMessage = "shop.protection.sign_gone";

/// 按钮按下之后干什么：买卖各自弹份数表单，管理入口进管理表单，管理表单上的按钮进各自的输入框。
///
/// 这里是**动作分发**的唯一一处：加动作时编译器会提醒这里漏了一个。
/// 两张表单共用它：管理表单上的动作不会出现在第一张表单上，但接线是同一条。
/// （动作×文案/校验的那张表不在这里，在 `manageInputField`：一个是分发，一个是数据。）
/// 回调可能在非主线程触发（DESIGN §14），所以这里不碰世界，只把工作交给 `request*`。
void onButton(Player& player, Cell const& sign, ShopFormAction action) {
    switch (action) {
    case ShopFormAction::Buy:
        requestBuyForm(player, sign);
        return;
    case ShopFormAction::Sell:
        requestSellForm(player, sign);
        return;
    case ShopFormAction::Manage:
        requestManageForm(player, sign);
        return;
    case ShopFormAction::ManageBuyPrice:
    case ShopFormAction::ManageSellPrice:
    case ShopFormAction::ManageAmount:
        requestManageInputForm(player, sign, action);
        return;
    case ShopFormAction::ToggleClosed:
        // 停业不需要输入框：直接走提交那一段（与三个输入框共用 `submitManageEdit` 的链）。
        requestManageToggleClosed(player, sign);
        return;
    case ShopFormAction::ManageRefreshItemName:
        // 与停业同构：没有输入框，直接把牌面第 4 行按箱子里的商品重写。
        requestManageRefreshItemName(player, sign);
        return;
    }
}

/// 表单回调不能假设线程（DESIGN §14）：回调里**只读 XUID 与维度两个纯标量**（不读方块、不读容器、不发包），
/// 真正要读世界或写牌时先切回服务端线程。
///
/// 与交易的 `onServerThread` 同一条口径：玩家下线、换了维度、配置不可用都直接放弃——
/// 这时那块牌已经不在他眼前，弹表单或写牌都没有意义。换维度时会给玩家一句提示（发包在服务端线程）。
void onSignServerThread(Player& player, std::function<void(Player&, ShopConfig const&)> task) {
    std::string const xuid      = player.getXuid();
    int const         dimension = player.getDimensionId();

    ll::thread::ServerThreadExecutor::getDefault().execute([xuid, dimension, task = std::move(task)] {
        auto& logger       = Shop::getInstance().getSelf().getLogger();
        auto const& config = Shop::getInstance().config();
        if (!config) {
            logger.debug(ui::msg("shop.log.form.dropped_no_config", xuid));
            return;
        }
        Player* resolved = onlinePlayer(xuid);
        if (resolved == nullptr) {
            logger.debug(ui::msg("shop.log.form.dropped_offline", xuid));
            return;
        }
        if (resolved->getDimensionId() != dimension) {
            // 与管理/交易两条链同一口径：写了也不生效（那块牌子不在他眼前），
            // 但必须说一句——静默 `return` 会让玩家以为按钮坏了。
            ui::send(*resolved, ui::Tone::Warning, "shop.trade.wrong_dimension");
            return;
        }
        task(*resolved, *config);
    });
}

/// 下拉框的回调结果读成下标；读不到就当「没选」（交给 `resolveRequestedUnits` 报错）。
///
/// **LeviLamina 回传的是选中项的文案，不是下标**：`Dropdown::parseResult` 里返回的是
/// `mOptions[index]`（字符串）。旧版本/别处直接给下标的情况也一并接住，两种都认。
int dropdownIndex(ll::form::CustomFormResult const& result, UnitsFormModel const& model) {
    if (!result) {
        return -1;
    }
    auto it = result->find(std::string{kUnitChoiceField});
    if (it == result->end()) {
        return -1;
    }
    if (auto const* index = std::get_if<uint64>(&it->second)) {
        return static_cast<int>(*index);
    }
    if (auto const* index = std::get_if<double>(&it->second)) {
        return static_cast<int>(*index);
    }
    if (auto const* text = std::get_if<std::string>(&it->second)) {
        return choiceIndexForOption(model, *text);
    }
    return -1;
}

/// 输入框的回调结果读成文本。
///
/// 返回空 = **拿不到这个控件的值**（没这个字段，或者类型不是字符串），与「玩家留空」不是一回事：
/// 空输入框是合法的「不做这一侧生意」，读不到却是表单出问题了，两者不能混成一个空串
/// （DESIGN §10.1 的坑：把下拉当 `uint64` 读，恒为 -1）。
std::optional<std::string> formInput(ll::form::CustomFormResult const& result, std::string_view field) {
    if (!result) {
        return std::nullopt;
    }
    auto it = result->find(std::string{field});
    if (it == result->end()) {
        return std::nullopt;
    }
    if (auto const* text = std::get_if<std::string>(&it->second)) {
        return *text;
    }
    return std::nullopt;
}

/// 供应 `inputText` 的份数表单用：读不到就是空串（交给 `resolveRequestedUnits` 报「没填」）。
std::string inputText(ll::form::CustomFormResult const& result) {
    return formInput(result, kCustomUnitsField).value_or(std::string{});
}

/// 重新看一眼这块牌，得出「能不能管理」的结论。`message` 非空 = 给玩家一句话就此结束。
///
/// 每一次进管理表单、每一次提交都重新走这一遍：表单可能挂了一阵子，店可能已经换主、
/// 箱子可能已经被拆。判据不重写：身份问 `canManageShop`（spec 的「判据注意」）。
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

/// 把表单提交交给对应的方向。买卖两条路只差这一处，别为它各写一整张表单。
void submit(Player& player, Cell const& sign, TradeDirection direction, TradeSubmission const& submission) {
    if (direction == TradeDirection::Sell) {
        submitSell(player, sign, submission);
        return;
    }
    submitBuy(player, sign, submission);
}

/// 表单上显示的那个金额（买入是扣款、卖出是收入）。价格缺失时没有可对照的数。
std::optional<std::int64_t> visibleTotal(ShopInfoData const& data, TradeDirection direction, int units) {
    auto const price = unitPriceOf(data, direction);
    if (!price) {
        return std::nullopt;
    }
    return totalPrice(*price, units);
}

} // namespace

void openShopForm(Player& viewer, Cell const& sign, ShopInfoData const& data, ShopFormRole role, bool canManage) {
    auto const localeCode = viewer.getLocaleCode();
    ll::form::SimpleForm form{
        text::render(shopFormTitle(data), localeCode),
        text::renderLines(shopFormContent(data), localeCode)
    };

    for (auto const& button : shopFormButtons(data, role, canManage)) {
        // 按钮回调可能在非主线程触发：回调里不碰方块与容器。
        // 买卖把工作交给 `requestBuyForm` / `requestSellForm`，管理入口交给 `requestManageForm`
        // （两者都会先切回服务端线程）。
        form.appendButton(text::render(button.label, localeCode), [action = button.action, sign](Player& player) {
            onButton(player, sign, action);
        });
    }

    form.sendTo(viewer);
}

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

void openTradeForm(Player& viewer, Cell const& sign, TradeDirection direction, ShopInfoData const& data, int maxUnits) {
    auto const model = makeUnitsFormModel(maxUnits);

    auto const localeCode = viewer.getLocaleCode();
    ll::form::CustomForm form{text::render(shopFormTitle(data), localeCode)};
    form.appendLabel(text::renderLines(unitsFormContent(direction, data, model), localeCode));
    form.appendDropdown(
        std::string{kUnitChoiceField},
        text::render(text::keyed(
            direction == TradeDirection::Buy ? "shop.form.units_field.buy" : "shop.form.units_field.sell",
            ss::shop::Tone::Label
        )),
        unitsFormOptions(model, localeCode),
        /*defaultVal=*/0
    );
    form.appendInput(
        std::string{kCustomUnitsField},
        text::render(
            text::sequence({
                text::keyed("shop.form.custom_units", ss::shop::Tone::Label),
                text::keyed("shop.form.custom_units_hint", ss::shop::Tone::Muted),
            }),
            localeCode
        ),
        "1"
    );

    form.sendTo(viewer, [sign, model, data, direction](Player& player, ll::form::CustomFormResult const& result, auto) {
        // 玩家选了「取消」：什么都不做，别发一条莫名其妙的提示。
        if (!result) {
            return;
        }
        auto const requested = resolveRequestedUnits(model, dropdownIndex(result, model), inputText(result));
        if (!requested.units) {
            // 同上：表单回调线程不确定，发包走服务端线程。
            onSignServerThread(player, [error = requested.error](Player& who, ShopConfig const&) {
                ui::send(who, ui::Tone::Failure, error);
            });
            return;
        }
        // 把「表单上看到的金额」与「共 N 个」一起交上去：两屏之间箱子里的货、店主改价或
        // 改份量都会让它们过期，`buyUnits` / `sellUnits` 会先返回确认表单，
        // 而不是按新价直接成交。
        // 与份数表单上「共 N 个」共用 `itemsForUnits`：两屏写同一个数，且用 int64 算再夹到 int。
        int const expectedItems = itemsForUnits(*requested.units, data.amountPerUnit);
        submit(
            player,
            sign,
            direction,
            TradeSubmission{
                *requested.units,
                expectedItems,
                visibleTotal(data, direction, *requested.units),
                /*confirmed=*/false
            }
        );
    });
}

void openTradeConfirmForm(
    Player&            viewer,
    Cell const&        sign,
    TradeDirection     direction,
    TradePlan const&   plan,
    Text const&        content
) {
    auto const localeCode = viewer.getLocaleCode();
    ll::form::SimpleForm form{
        text::render(text::keyed("shop.form.confirm_title", ss::shop::Tone::Title), localeCode),
        text::render(content, localeCode)
    };

    // 按钮文字不上色：按钮底有浅灰与深绿两种状态，固定颜色必在其一里失效。
    auto const verb = text::render(text::keyed(
        direction == TradeDirection::Buy ? "shop.form.confirm_buy" : "shop.form.confirm_sell",
        ss::shop::Tone::Default,
        {text::lit(std::to_string(plan.units))}
    ), localeCode);
    form.appendButton(verb, [sign, plan, direction](Player& player) {
        // 已经看过真实份数与金额了：带着它们再确认一次，后续若又变就按实际成交并报告。
        submit(
            player,
            sign,
            direction,
            TradeSubmission{
                plan.units,
                plan.items,
                std::optional<std::int64_t>{plan.total},
                /*confirmed=*/true
            }
        );
    });
    form.appendButton(text::render(text::keyed("shop.form.cancel", ss::shop::Tone::Default), localeCode), [](Player&) {});

    form.sendTo(viewer);
}

} // namespace ss::shop
