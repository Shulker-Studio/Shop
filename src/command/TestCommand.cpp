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

/// 找不到商店牌时说的一句。命令输出不走 i18n（它是给开发者看的），
/// 但余额与状态仍是数字，直接写进 `output`。
constexpr int kScanRadius = 4;

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

/// 背包一格空位都没有。补偿场景靠它保证「发货必然失败」。
bool inventoryFull(Player& player) {
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
        output.success(std::string{"成交："} + detail);
        return;
    }
    output.error(std::string{"未成交（"} + std::string{statusName(result.status)} + "）：" + detail);
}

void scenarioBuy(Player& player, CommandOutput& output, ShopConfig const& config, Cell const& sign) {
    auto const shop = readShop(player.getDimensionBlockSource(), toBlockPos(sign), config);
    if (!shop) {
        output.error("这块牌现在读不出商店");
        return;
    }
    auto const submission = buyOneSubmission(shop->info);
    if (!submission) {
        output.error("这家店没有买入价");
        return;
    }
    reportResult(output, buyUnits(player.getDimensionBlockSource(), sign, player, config, *submission));
}

/// 补偿场景：扣钱成功、发货失败时钱必须原样退回。
///
/// 发货失败的唯一现实原因是背包放不下，所以这里**不替玩家塞背包**（那会毁掉他的物品），
/// 只在他自己塞满之后跑一次买入，然后比对余额。
void scenarioCompensate(Player& player, CommandOutput& output, ShopConfig const& config, Cell const& sign) {
    if (!inventoryFull(player)) {
        output.error("先把背包塞满（发货必须失败），再执行 /shoptest compensate");
        return;
    }
    auto const shop = readShop(player.getDimensionBlockSource(), toBlockPos(sign), config);
    if (!shop) {
        output.error("这块牌现在读不出商店");
        return;
    }
    auto const submission = buyOneSubmission(shop->info);
    if (!submission) {
        output.error("这家店没有买入价");
        return;
    }

    auto const   before  = economyBackend().get(player.getXuid());
    TradeResult  result = buyUnits(player.getDimensionBlockSource(), sign, player, config, *submission);
    auto const   after   = economyBackend().get(player.getXuid());

    reportResult(output, result);
    if (!before || !after) {
        output.error("读不到余额，无法判断是否退款");
        return;
    }
    if (*before == *after) {
        output.success("退款正确：余额未变（" + std::to_string(*before) + "）");
        return;
    }
    output.error("余额从 " + std::to_string(*before) + " 变成 " + std::to_string(*after) + "：没有退款");
}

void runScenario(CommandOrigin const& origin, CommandOutput& output, std::string_view scenario) {
    auto* actor = origin.getEntity();
    if (actor == nullptr || !actor->isPlayer()) {
        output.error("/shoptest 只能由玩家执行");
        return;
    }
    auto& player = static_cast<Player&>(*actor);

    auto const& config = Shop::getInstance().config();
    if (!config) {
        output.error("模组未启用（配置不可用）");
        return;
    }

    auto const sign = findNearbyShop(player, *config);
    if (!sign) {
        output.error("周围 " + std::to_string(kScanRadius) + " 格内没有可交易的商店牌");
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
    output.error("未知场景：只有 buy / compensate");
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
                            "商店交易的真机自检（buy / compensate）",
                            CommandPermissionLevel::GameDirectors
                        );

    command.overload<TestParam>().required("scenario").execute(
        [](CommandOrigin const& origin, CommandOutput& output, TestParam const& param) {
            runScenario(origin, output, param.scenario);
        }
    );
}

} // namespace ss::shop
