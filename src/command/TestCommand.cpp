#include "command/TestCommand.h"

#include "core/economy/Service.h"
#include "core/container/ContainerFinder.h"
#include "core/shop/ShopReader.h"
#include "core/sign/SignGeometry.h"
#include "core/trade/TradePricing.h"
#include "core/trade/TradeService.h"
#include "form/UnitsForm.h"
#include "util/i18n/Locale.h"
#include "util/i18n/Text.h"

#include "Shop.h"

#include "ll/api/command/CommandHandle.h"
#include "ll/api/command/CommandRegistrar.h"

#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/server/commands/CommandPermissionLevel.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace ss::shop {

struct TestParam {
    std::string scenario;
};

namespace {

constexpr int kScanRadius = 4;

/// `/shoptest` 的输出同样走词表：它虽然只给管理员看，但项目里只有一条文案通道
/// （`Locale.cpp` 的两条 bootstrap 日志是唯一例外，那两条不能查表）。
Text out(std::string_view key) {
    return text::keyed(std::string{key});
}

/// 带一个字符串参数（数字、状态名）。
Text out(std::string_view key, std::string value) {
    return text::keyed(std::string{key}, Tone::Default, {text::lit(std::move(value))});
}

/// 带两个字符串参数。
Text out(std::string_view key, std::string first, std::string second) {
    return text::keyed(std::string{key}, Tone::Default, {text::lit(std::move(first)), text::lit(std::move(second))});
}

/// 在玩家周围找第一块**可交易**的商店牌。
///
/// `readShop` 对不是商店牌的格子几乎零成本（牌文本不合法就直接返回），
/// 所以按格扫一遍比先做视线射线简单可靠——命令是手动的，慢一点无所谓。
std::optional<Cell> findNearbyShop(Player& player, ShopConfig const& config) {
    auto const        origin = player.getPosition();
    auto&             region = player.getDimensionBlockSource();
    BlockPos const    base{origin.x, origin.y, origin.z};

    for (int dy = -2; dy <= 2; ++dy) {
        for (int dx = -kScanRadius; dx <= kScanRadius; ++dx) {
            for (int dz = -kScanRadius; dz <= kScanRadius; ++dz) {
                BlockPos const pos{base.x + dx, base.y + dy, base.z + dz};
                if (readShop(region, pos, config)) {
                    return Cell{pos.x, pos.y, pos.z};
                }
            }
        }
    }
    return std::nullopt;
}

/// 背包一格空位都没有。
///
/// **这不等于「发货必然失败」**：同类物品的部分堆叠仍能接收新货。所以补偿场景
/// 不拿它当结论，只当前置筛选，真正的判定看 `buyUnits` 返回的状态。
bool inventoryHasNoEmptySlots(Player& player) {
    auto& inventory = player.getInventory();
    int   count     = inventory.getContainerSize();
    for (int slot = 0; slot < count; ++slot) {
        if (inventory.getItem(slot).isNull()) {
            return false;
        }
    }
    return true;
}

std::string_view statusName(TradeStatus status) {
    switch (status) {
    case TradeStatus::Done:
        return "Done";
    case TradeStatus::Rejected:
        return "Rejected";
    case TradeStatus::NeedsConfirm:
        return "NeedsConfirm";
    case TradeStatus::Broken:
        return "Broken";
    }
    return "?";
}

/// 按「买 1 份」构造一次确认过的提交。
///
/// `confirmed = true`：命令就是确认本身，不再弹二次确认表单。
std::optional<TradeSubmission> buyOneSubmission(ShopInfoData const& info) {
    auto const price = unitPriceOf(info, TradeDirection::Buy);
    if (!price) {
        return std::nullopt;
    }
    int const units = 1;
    return TradeSubmission{
        units,
        itemsForUnits(units, info.amountPerUnit),
        totalPrice(*price, units),
        /*confirmed=*/true,
    };
}

void reportResult(CommandOutput& output, TradeResult const& result) {
    std::string const detail = ui::plain(result.message);
    if (result.status == TradeStatus::Done) {
        output.success(ui::plain(out("shop.command.test.done", detail)));
        return;
    }
    output.error(ui::plain(out("shop.command.test.not_done", std::string{statusName(result.status)}, detail)));
}

void scenarioBuy(Player& player, CommandOutput& output, ShopConfig const& config, Cell const& sign) {
    auto const shop = readShop(player.getDimensionBlockSource(), toBlockPos(sign), config);
    if (!shop) {
        output.error(ui::plain(out("shop.command.test.unreadable_shop")));
        return;
    }
    auto const submission = buyOneSubmission(shop->info);
    if (!submission) {
        output.error(ui::plain(out("shop.command.test.no_buy_price")));
        return;
    }
    reportResult(output, buyUnits(player.getDimensionBlockSource(), sign, player, config, *submission));
}

/// 补偿场景：扣钱成功、发货失败时钱必须原样退回。
///
/// 发货失败的现实原因是背包放不下，所以这里**不替玩家塞背包**（那会毁掉他的物品），
/// 只在他自己塞满之后跑一次买入。
///
/// 「塞满」只是前置筛选，不是结论：同类物品的部分堆叠仍能接收新货，那一单会正常成交。
/// 因此只有 `buyUnits` 明确失败时才比对余额——成交时余额变了是正常交易，不是没退款。
void scenarioCompensate(Player& player, CommandOutput& output, ShopConfig const& config, Cell const& sign) {
    if (!inventoryHasNoEmptySlots(player)) {
        output.error(ui::plain(out("shop.command.test.need_full")));
        return;
    }
    auto const shop = readShop(player.getDimensionBlockSource(), toBlockPos(sign), config);
    if (!shop) {
        output.error(ui::plain(out("shop.command.test.unreadable_shop")));
        return;
    }
    auto const submission = buyOneSubmission(shop->info);
    if (!submission) {
        output.error(ui::plain(out("shop.command.test.no_buy_price")));
        return;
    }

    auto const  before = economyBackend().get(player.getXuid());
    TradeResult result = buyUnits(player.getDimensionBlockSource(), sign, player, config, *submission);
    auto const  after  = economyBackend().get(player.getXuid());

    reportResult(output, result);

    if (result.status == TradeStatus::Done) {
        // 背包里还有能接收这批货的部分堆叠：这一次走的是正常路径，没进入补偿。
        output.error(ui::plain(out("shop.command.test.sold_through")));
        return;
    }
    if (!before || !after) {
        output.error(ui::plain(out("shop.command.test.no_balance")));
        return;
    }
    if (*before == *after) {
        output.success(ui::plain(out("shop.command.test.refund_ok", std::to_string(*before))));
        return;
    }
    output.error(ui::plain(out("shop.command.test.refund_missing", std::to_string(*before), std::to_string(*after))));
}

void runScenario(CommandOrigin const& origin, CommandOutput& output, std::string_view scenario) {
    auto* actor = origin.getEntity();
    if (actor == nullptr || !actor->isPlayer()) {
        output.error(ui::plain(out("shop.command.test.only_player")));
        return;
    }
    auto& player = static_cast<Player&>(*actor);

    auto const& config = Shop::getInstance().config();
    if (!config) {
        output.error(ui::plain(out("shop.command.test.no_config")));
        return;
    }

    auto const sign = findNearbyShop(player, *config);
    if (!sign) {
        output.error(ui::plain(out("shop.command.test.no_shop", std::to_string(kScanRadius))));
        return;
    }

    if (scenario == "buy") {
        scenarioBuy(player, output, *config, *sign);
        return;
    }
    if (scenario == "compensate") {
        scenarioCompensate(player, output, *config, *sign);
        return;
    }
    output.error(ui::plain(out("shop.command.test.unknown")));
}

} // namespace

void registerTestCommand() {
    // 重载模组会再跑一遍 enable()：命令句柄是全局的，重复 overload 会让一条命令执行两遍。
    static bool registered = false;
    if (registered) {
        return;
    }
    registered = true;

    auto& command = ll::command::CommandRegistrar::getInstance(/*isClientSide=*/false)
                        .getOrCreateCommand(
                            "shoptest",
                            ui::plain(text::keyed("shop.command.test.description")),
                            CommandPermissionLevel::GameDirectors
                        );

    command.overload<TestParam>().required("scenario").execute(
        [](CommandOrigin const& origin, CommandOutput& output, TestParam const& param) {
            runScenario(origin, output, param.scenario);
        }
    );
}

} // namespace ss::shop
