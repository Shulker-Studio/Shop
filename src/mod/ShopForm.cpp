#include "mod/ShopForm.h"

#include "mod/BlockResync.h"
#include "mod/ContainerFinderBds.h"
#include "mod/EconomyService.h"
#include "mod/Shop.h"
#include "mod/ShopOwner.h"
#include "mod/ShopService.h"
#include "mod/TradeService.h"
#include "shop/ManageForm.h"
#include "shop/SignProtection.h"
#include "shop/TradePricing.h"
#include "shop/UnitsForm.h"

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

/// 提交时牌子已经不在了。
constexpr std::string_view kSignGoneMessage = "这块商店牌已经不在了，改价没有生效";

/// 按钮按下之后干什么：买卖各自弹份数表单，管理入口进管理表单。
///
/// 一个 switch 收口所有动作：加动作时编译器会提醒这里漏了一个。
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
        requestBuyPriceForm(player, sign);
        return;
    case ShopFormAction::ManageSellPrice:
    case ShopFormAction::ManageAmount:
    case ShopFormAction::ToggleClosed:
        // 02 / 03 接上之前，没有任何表单会发出这三个动作。
        return;
    }
}

/// 管理表单上的按钮，与 `onButton` 同一套理由。
void onManageButton(Player& player, Cell const& sign, ShopFormAction action) {
    if (action == ShopFormAction::ManageBuyPrice) {
        requestBuyPriceForm(player, sign);
    }
}

/// 表单回调不能假设线程（DESIGN §14）：回调里**一个世界状态都不碰**，只捕
/// 「谁的 XUID、哪个维度、哪块牌」，真正要读方块/写牌时先切回服务端线程。
///
/// 与交易的 `onServerThread` 同一条口径：玩家下线、换了维度、配置不可用都直接放弃——
/// 这时那块牌已经不在他眼前，弹表单或写牌都没有意义。
void onSignServerThread(Player& player, std::function<void(Player&, ShopConfig const&)> task) {
    std::string const xuid      = player.getXuid();
    int const         dimension = player.getDimensionId();

    ll::thread::ServerThreadExecutor::getDefault().execute([xuid, dimension, task = std::move(task)] {
        auto const& config = Shop::getInstance().config();
        if (!config) {
            return;
        }
        Player* resolved = onlinePlayer(xuid);
        if (resolved == nullptr || resolved->getDimensionId() != dimension) {
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
    std::string                 message;
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

/// 管理表单的提交：重新解析牌面、合成新文本、写牌、把这一格发给整维度。
///
/// 提交时**从牌面重新读一遍**，不用打开表单时缓存的 `ShopInfoData`：表单开着的时候
/// 牌可能已经被别人改过（DESIGN §10.4 的同一条口径）。
void submitBuyPrice(Player& player, Cell const& sign, std::string input) {
    onSignServerThread(player, [sign, input = std::move(input)](Player& who, ShopConfig const& config) {
        auto&          region = who.getDimensionBlockSource();
        BlockPos const pos    = toBlockPos(sign);

        auto* signActor = signBlockActorAt(region, pos);
        if (signActor == nullptr) {
            who.sendMessage(std::string{kSignGoneMessage});
            return;
        }

        auto const target = inspectManageTarget(region, pos, who, config);
        if (!target.shop) {
            who.sendMessage(target.message);
            return;
        }

        auto const signConfig = toSignConfig(config);
        auto const text       = signActor->getMessage(SignTextSide::Front);
        // `editorIsOp` 固定 `true`：它只影响「是不是管理员店」，与此刻是谁在改无关
        // （`parseShopSignFront` 同一条理由）。解析出的 `adminShop` 也不参与出文本
        // （`formatShopSign` 不看它）；真正决定写牌 XUID 的是 `inspectShop` 读出的 `info.adminShop`。
        auto const parsed = parseShopSignText(text, /*editorIsOp=*/true, signConfig);
        if (!parsed.ok()) {
            who.sendMessage(shopInvalidMessage());
            return;
        }

        auto const edit = withBuyPrice(*parsed.spec, input, signConfig);
        if (!edit.ok()) {
            // 失败只提示，牌面一字不改（spec D6）。
            who.sendMessage(edit.error);
            return;
        }

        // 规范化回写：第 1 行（店主名）与第 4 行（商品名）沿用牌面原文。
        // 商品由容器决定，店主名不该被一次改价动到。
        auto const lines     = splitSignText(text);
        auto const formatted = formatShopSign(*edit.spec, lines[SignLine::OwnerLine], lines[SignLine::ItemLine]);

        // 管理员店的归属靠「第 1 行 == adminShopName + 编辑者是 OP」，不靠 XUID
        // （流水里管理员店的 `owner.xuid` 也是空串）。
        std::string const ownerId = target.shop->info.adminShop ? std::string{} : target.shop->ownerXuid;
        signActor->setMessageForServerScripingOnly(SignTextSide::Front, joinSignText(formatted), std::move(ownerId));

        // 这次写入不是玩家发起的编辑，客户端不会自己重画：按 DESIGN §9.2 发给整个维度。
        resyncBlockActorToDimension(region, pos);

        if (edit.spec->buyPrice) {
            who.sendMessage("买入价已改为 " + moneyText(config.economy.currencySymbol, *edit.spec->buyPrice) + " / 份");
        } else {
            who.sendMessage("买入价已清空：这一侧不再做生意");
        }
    });
}

void openManageForm(Player& player, Cell const& sign, ShopInfoData const& data, bool waxed) {
    ll::form::SimpleForm form{shopFormTitle(data) + "（管理）", shopManageFormContent(data, waxed)};

    for (auto const& button : shopManageFormButtons(data)) {
        form.appendButton(button.label, [action = button.action, sign](Player& who) {
            onManageButton(who, sign, action);
        });
    }

    form.sendTo(player);
}

void openBuyPriceForm(Player& player, Cell const& sign, ShopInfoData const& data, bool waxed) {
    ll::form::CustomForm form{shopFormTitle(data) + "（管理 · 改买入价）"};
    // 正文与信息表单同源：改价时看得到「箱里还剩几份」「余额够不够」，改完不用再点回来看。
    form.appendLabel(shopManageFormContent(data, waxed));

    std::string const current = data.buyPrice ? std::to_string(*data.buyPrice) : std::string{};
    form.appendInput(
        std::string{kBuyPriceField},
        "买入价（一份的总价，玩家买一份付给你多少）",
        /*placeholder=*/data.buyPrice ? current : std::string{"留空表示这一侧不出售"},
        /*defaultVal=*/current
    );

    form.sendTo(player, [sign](Player& who, ll::form::CustomFormResult const& result, auto) {
        // 玩家选了「取消」：什么都不做，别发一条莫名其妙的提示。
        if (!result) {
            return;
        }
        if (auto const input = formInput(result, kBuyPriceField)) {
            submitBuyPrice(who, sign, *input);
            return;
        }
        // 读不到字符串就是表单出问题了：按「输入不合法」处理，不静默当留空。
        who.sendMessage("读不到买入价这个输入框的值，请重新打开表单再试");
    });
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
    ll::form::SimpleForm form{shopFormTitle(data), shopFormContent(data)};

    for (auto const& button : shopFormButtons(data, role, canManage)) {
        // 按钮回调可能在非主线程触发：回调里不碰方块与容器。
        // 买卖把工作交给 `requestBuyForm` / `requestSellForm`，管理入口交给 `requestManageForm`
        // （两者都会先切回服务端线程）。
        form.appendButton(button.label, [action = button.action, sign](Player& player) {
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
            player.sendMessage(target.message);
            return;
        }
        openManageForm(player, sign, target.shop->info, target.waxed);
    });
}

void requestBuyPriceForm(Player& viewer, Cell const& sign) {
    onSignServerThread(viewer, [sign](Player& player, ShopConfig const& config) {
        auto&          region = player.getDimensionBlockSource();
        BlockPos const pos    = toBlockPos(sign);

        auto const target = inspectManageTarget(region, pos, player, config);
        if (!target.shop) {
            player.sendMessage(target.message);
            return;
        }
        openBuyPriceForm(player, sign, target.shop->info, target.waxed);
    });
}

void openTradeForm(Player& viewer, Cell const& sign, TradeDirection direction, ShopInfoData const& data, int maxUnits) {
    auto const model = makeUnitsFormModel(maxUnits);

    ll::form::CustomForm form{shopFormTitle(data)};
    form.appendLabel(unitsFormContent(direction, data, model));
    form.appendDropdown(
        std::string{kUnitChoiceField},
        std::string{directionVerb(direction)} + "几份",
        unitsFormOptions(model),
        /*defaultVal=*/0
    );
    form.appendInput(std::string{kCustomUnitsField}, "自定义份数（选「自定义份数」时才看这里）", "1");

    form.sendTo(viewer, [sign, model, data, direction](Player& player, ll::form::CustomFormResult const& result, auto) {
        // 玩家选了「取消」：什么都不做，别发一条莫名其妙的提示。
        if (!result) {
            return;
        }
        auto const requested = resolveRequestedUnits(model, dropdownIndex(result, model), inputText(result));
        if (!requested.units) {
            player.sendMessage(requested.error);
            return;
        }
        // 把「表单上看到的金额」与「共 N 个」一起交上去：两屏之间箱子里的货、店主改价或
        // 改份量都会让它们过期，`buyUnits` / `sellUnits` 会先返回确认表单，
        // 而不是按新价直接成交。
        int const expectedItems = *requested.units * data.amountPerUnit;
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
    std::string const& content
) {
    ll::form::SimpleForm form{"确认交易", content};

    auto const verb = "确认" + std::string{directionVerb(direction)} + "（";
    form.appendButton(verb + std::to_string(plan.units) + " 份）", [sign, plan, direction](Player& player) {
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
    form.appendButton("取消", [](Player&) {});

    form.sendTo(viewer);
}

} // namespace ss::shop
