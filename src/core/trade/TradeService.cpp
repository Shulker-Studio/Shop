#include "core/trade/TradeService.h"
#include "util/i18n/Locale.h"
#include "util/i18n/Message.h"

#include "util/time/Time.h"
#include "core/container/ContainerFinder.h"
#include "util/log/Debug.h"
#include "util/log/Discard.h"
#include "core/economy/Service.h"
#include "Shop.h"
#include "form/ShopForm.h"
#include "core/shop/ShopReader.h"
#include "core/shop/ShopInspector.h"
#include "util/log/Transaction.h"
#include "util/time/Time.h"
#include "core/shop/ShopInfoData.h"
#include "core/shop/ShopFormRole.h"
#include "core/trade/UnitCount.h"
#include "util/i18n/Reason.h"
#include "core/container/SampleSlot.h"
#include "core/trade/ClickCooldown.h"
#include "core/trade/TradePlan.h"
#include "core/trade/TradePricing.h"
#include "form/UnitsForm.h"

#include "ll/api/service/Bedrock.h"
#include "ll/api/thread/ServerThreadExecutor.h"

#include "mc/world/Container.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/actor/BlockActor.h"

#include <algorithm>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <utility>
#include <vector>

namespace ss::shop {
namespace {

/// 提交冷却。与「点牌」的连点冷却分开：点开表单后立刻按「购买」是两个动作，
/// 共用一个窗口会把正经操作误伤掉（DESIGN §10.4）。
std::optional<ClickCooldown> gSubmitGate;

/// 被冻结的「玩家 + 商店」。
///
/// 补偿失败意味着钱或货可能已经错位（DESIGN §10.2 第 8 步），这时候让玩家重试只会把
/// 事故扩大，所以在内存里钉住这一对，后续提交直接拒绝。
///
/// **生命周期**：`disable()` 不再主动清（以前会，于是 `/ll reload` 等于静默解冻）。
/// 但这**不等于**它一定能跨 reload 活下来：加载器若把 DLL 整块卸载再载入，静态对象仍会随之重置。
/// 要真跨过那个边界只能落盘（目前未做），所以卸载时打一条 WARN 给出冻结对数。
/// 买卖共用这一份名单：一次失败冻结的是这一对「玩家 + 商店」，不是某个方向。
std::set<std::string> gFrozenTrades;

/// 冻结键 = 玩家 + 维度 + 牌位。
///
/// 维度必须进键：`Cell` 只有整数坐标，主世界与下界/末地可能各有一块同坐标的商店牌，
/// 少了它，一次主世界的补偿失败会让玩家在下界那家完全无关的店也交易不了，
/// 而提示写的是「上一次交易未完成」——玩家根本想不起来是哪一次。
std::string freezeKey(std::string_view xuid, int dimensionId, Cell const& sign) {
    return std::string{xuid} + "@" + std::to_string(dimensionId) + "@" + std::to_string(sign.x) + ","
         + std::to_string(sign.y) + "," + std::to_string(sign.z);
}

bool isFrozen(std::string_view xuid, int dimensionId, Cell const& sign) {
    return gFrozenTrades.contains(freezeKey(xuid, dimensionId, sign));
}

/// 把这一格的方块实体刷给客户端（DESIGN §10.2 第 9 步）。
///
/// `Container::removeItem` 内部会走 `setContainerChanged`，但双箱是**两个**方块实体，
/// DESIGN §7.3 要求「分别 setItem 并各自 refresh」——所以两半各刷一次，不指望引擎代劳。
void refreshBlockActor(BlockSource& region, Cell const& cell) {
    if (auto* actor = region.getBlockEntity(toBlockPos(cell)); actor != nullptr) {
        actor->refresh(region);
    }
}

/// 把商店箱（双箱两半各自）刷给客户端。
void refreshShopBox(BlockSource& region, AttachedContainer const& attached) {
    refreshBlockActor(region, attached.ref.pos);
    if (attached.ref.pairedPos) {
        refreshBlockActor(region, *attached.ref.pairedPos);
    }
}

/// 整批槽位的副本，失败时用来把物品原样写回（DESIGN §10.2 第 4 步）。
///
/// 用「整组槽位」而不是「本次动过的那几格」：`Container::addItem` 会自己挑格子叠放，
/// 事先猜不到它碰了哪些槽位，猜错就会漏回写。
struct SlotSnapshot {
    Container*             container = nullptr;
    std::vector<ItemStack> copies;

    explicit SlotSnapshot(Container& from) : container(&from), copies(from.getSlotCopies()) {}

    void restore() const {
        for (int slot = 0; slot < static_cast<int>(copies.size()); ++slot) {
            container->setItem(slot, copies[static_cast<std::size_t>(slot)]);
        }
    }
};

/// 商店箱（双箱两半各一份）+ 玩家背包的快照。买卖两边都要这两样：
/// 买入动的是「箱子减、背包加」，卖出动的是「背包减、箱子加」。
struct TradeSnapshots {
    std::vector<SlotSnapshot> shopBox;
    SlotSnapshot              backpack;

    TradeSnapshots(AttachedContainer const& attached, Inventory& inventory) : backpack(inventory) {
        shopBox.reserve(2);
        shopBox.emplace_back(*attached.primary);
        if (attached.secondary != nullptr) {
            shopBox.emplace_back(*attached.secondary);
        }
    }

    void restoreShopBox() const {
        for (auto const& snapshot : shopBox) {
            snapshot.restore();
        }
    }
};

/// 世界状态导致的拒绝原因（流水 `reason` 列里「不是 `TradeRejection` 那几类」的部分）。
///
/// 为什么不全用 `TradeRejection`：那个枚举只描述**由数字判定**的原因（份数、余额、货、箱空间、税），
/// 而这里这些是**世界状态**（牌没了、后端没起来、创造模式、冻结名单…）——`planTrade` 看不到它们，
/// 也不该看到。但流水需要一个稳定的键，所以这些也得有短名，不能把中文文案写进 `reason`。
///
/// 两份表的分工："由数字判定的" 走 `tradeRejectionReasonName`（`src/shop`），
/// 这里只补前者表达不了的部分。两边产出同一种东西（一个英文小写下划线短名）。
enum class RejectReason : std::uint8_t {
    ShopMissing,        ///< 牌没了 / 已经不是一个可交易的商店
    ShopUnavailable,    ///< 这个方向不做这门生意（牌上只写了另一侧的价格）
    OwnerUnknown,       ///< 读不到店主（没人能收款 / 没人能付款）
    SelfTrade,          ///< 不能跟自己交易
    FrozenPair,         ///< 这一对「玩家 + 商店」已被冻结（上一次补偿失败的后续提交）
    CreativeMode,       ///< 创造模式不参与经济
    NoBackend,          ///< 没有可用的经济后端
    NoSystemAccount,    ///< 后端当不了管理员店的系统账户
    BoxMissing,         ///< 商店的箱子不见了
    NoProduct,          ///< 箱子里没有商品（卖光了 / 还没设商品）
    MixedContents,      ///< 商品是容器类物品但盒内不整齐（混装 / 每格数量不同）
    BannedItem,         ///< 商品被服主禁售（本身或盒内嵌套的物品命中禁售表）
    BundleProduct,      ///< 商品是收纳袋（内容在服务端读不出来，一律不许当商品）
    ItemMismatch,       ///< 玩家手里有同类物品，但不是严格同种（附魔/耐久/自定义名/盒内内容对不上）
    PlayerFundsUnknown, ///< 读不到玩家余额（不是「余额是 0」）
    OwnerFundsUnknown,  ///< 读不到店主余额
    PaymentFailed,      ///< 付款那一步失败（不是后端坏了，是这一笔没成）
    TaxFailed,          ///< 商店税没收到
    /// 补偿失败 → `frozen`。与 fatal 日志**成对出现**：冻结必须有上游事故，
    /// 单独一条 `frozen` 找不到对应的事故说明它根本不是补偿失败。
    CompensationFailed,
};

std::string_view rejectReasonName(RejectReason reason) {
    switch (reason) {
    case RejectReason::ShopMissing:
        return "shop_missing";
    case RejectReason::ShopUnavailable:
        return "shop_unavailable";
    case RejectReason::OwnerUnknown:
        return "owner_unknown";
    case RejectReason::SelfTrade:
        return "self_trade";
    case RejectReason::FrozenPair:
        return "frozen_pair";
    case RejectReason::CreativeMode:
        return "creative_mode";
    case RejectReason::NoBackend:
        return "no_backend";
    case RejectReason::NoSystemAccount:
        return "no_system_account";
    case RejectReason::BoxMissing:
        return "box_missing";
    case RejectReason::NoProduct:
        return "no_product";
    case RejectReason::MixedContents:
        return "mixed_contents";
    case RejectReason::BannedItem:
        return "banned_item";
    case RejectReason::BundleProduct:
        return "bundle_product";
    case RejectReason::ItemMismatch:
        return "item_mismatch";
    case RejectReason::PlayerFundsUnknown:
        return "player_funds_unknown";
    case RejectReason::OwnerFundsUnknown:
        return "owner_funds_unknown";
    case RejectReason::PaymentFailed:
        return "payment_failed";
    case RejectReason::TaxFailed:
        return "tax_failed";
    case RejectReason::CompensationFailed:
        return "compensation_failed";
    }
    return {};
}

/// 拒绝：`message` 是给玩家看的中文，`logReason` 是给流水看的关键字。两件事必须分开——
/// 文案随时会改，历史流水的 `reason` 不能跟着改。
TradeResult reject(Text message, RejectReason reason) {
    return {.status = TradeStatus::Rejected, .message = std::move(message), .logReason = rejectReasonName(reason)};
}

/// 同上，但原因是 `planTrade` 判出来的（`TradeRejection` 那一套）。
TradeResult reject(Text message, TradeRejection rejection) {
    return {
        .status    = TradeStatus::Rejected,
        .message   = std::move(message),
        .logReason = tradeRejectionReasonName(rejection),
    };
}

/// 同上，但原因已经是短名（`undoAndRefund` 这类把上游的 `reason` 原样传下来的地方）。
TradeResult reject(Text message, std::string_view reason) {
    return {.status = TradeStatus::Rejected, .message = std::move(message), .logReason = reason};
}

/// 给结果补上流水事实。每一条失败路径都要带：一条 `fail` 不知道是哪个人、哪块牌就白记了。
TradeResult withFacts(TradeResult result, TradeFacts const& facts) {
    result.logFacts = facts;
    return result;
}

/// 把一条运维消息发给所有在线 OP（没有人能收就当无事发生，日志已经记了）。
void notifyOperators(std::string const& message) {
    auto level = ll::service::bedrock::getLevel();
    if (!level) {
        return;
    }
    level->forEachPlayer([&message](Player& player) {
        if (player.isOperator()) {
            // 运维消息是给 OP 看的诊断文本，不翻译；前缀与红色统一走 `ui::line`。
            ui::send(player, ui::Tone::Warning, message);
        }
        return true;
    });
}

/// 补偿失败：钱或货可能已经动了没还原（DESIGN §10.2 第 8 步）。
///
/// 三件事一起做：严重日志、通知在线 OP、把这一对「玩家 + 商店」钉进冻结名单。
/// 只记日志等于把事故留给服主自己偶遇，只冻结不通知等于玩家一脸茫然。
TradeResult broken(std::string const& freeze, Text message) {
    auto const plain = text::render(message);
    Shop::getInstance().getSelf().getLogger().fatal(ui::msg("shop.log.trade.compensation_failed", freeze, plain)
    );
    notifyOperators(ui::msg("shop.log.trade.compensation_failed", freeze, plain));
    gFrozenTrades.insert(freeze);
    // 流水里的 `frozen` 与上面这条 fatal 是**同一件事**：`reason: compensation_failed`
    // 就是「去 grep 日志里的 交易补偿失败」的指针。
    return {
        .status    = TradeStatus::Broken,
        .message   = std::move(message),
        .logReason = rejectReasonName(RejectReason::CompensationFailed),
    };
}

/// 一组容器里还能再装下几份商品。
///
/// 算的就是 `Container::addItem` 接下来会写的那同一批槽位：空格按满栈算，
/// 严格同种的已有堆叠按剩余空间算。所以这是**准确值**，不是估值。
/// `slots` 是合并后的总格数（单容器就是 `getContainerSize()`）。
int roomUnits(Container& primary, Container* secondary, int slots, ItemStack const& product, int amountPerUnit) {
    auto const   maxStack = static_cast<int>(product.getMaxStackSize());
    std::int64_t capacity = 0;
    for (auto const& slot : mergedSlots(primary, secondary, slots)) {
        ItemStack const& stack = slot.container->getItem(slot.slot);
        if (stack.isNull()) {
            capacity += maxStack;
        } else if (sameShopItem(stack, product)) {
            capacity += std::max(0, maxStack - static_cast<int>(stack.mCount));
        }
    }
    return unitsFromItems(
        static_cast<int>(std::min<std::int64_t>(capacity, std::numeric_limits<int>::max())),
        amountPerUnit
    );
}

/// 背包还能再装下几份商品（买入时买方收货的容量）。
///
/// `Player::getInventory()` 里的盔甲与副手是另外的容器、不参与叠放，
/// 所以这个数就是 `Inventory::add` 真的能用的空间。
int backpackUnits(Inventory& inventory, ItemStack const& product, int amountPerUnit) {
    return roomUnits(inventory, nullptr, inventory.getContainerSize(), product, amountPerUnit);
}

/// 商店箱还能再装下几份商品（卖出时商店收货的容量）。
///
/// 与背包容量是同一个算式，只是容器换成商店箱、格数换成合并后的边界：
/// 双箱两半按 `mergedSlots` 的顺序算，`Container::addItem` 也按同一个顺序找位置。
///
/// **不用 `Container::hasRoomForItem` 当容量判据**：它只看**单一半**箱子、
/// 而且要求整批能塞进一个容器；两半各剩 20 格时它能装下 40 件，单半判却是否定的，
/// 拿它当唯一判据会把合法的双箱卖单拒掉。真正的容量只在这里算一次，
/// 实际放货以 `addItem` 剩下的 `ItemStack` 为真值，放不下就按快照回写并退款。
int shopBoxUnits(AttachedContainer const& attached, ItemStack const& product, int amountPerUnit) {
    return roomUnits(*attached.primary, attached.secondary, attached.ref.size, product, amountPerUnit);
}

/// 玩家背包里有没有「同一种物品、但不算严格同种」的堆叠。
///
/// 只用来把「你手里根本没有」与「你手里这件的附魔 / 耐久 / 自定义名不对」分开说。
/// 判定看 id + aux，**不参与**交易逻辑：计数与扣货仍然只认 `sameShopItem`（DESIGN §5.3）。
bool holdsSimilarItem(Inventory& inventory, ItemStack const& product) {
    for (int slot = 0; slot < inventory.getContainerSize(); ++slot) {
        ItemStack const& stack = inventory.getItem(slot);
        bool const       sameKind =
            !stack.isNull() && stack.getId() == product.getId() && stack.getAuxValue() == product.getAuxValue();
        if (sameKind && !sameShopItem(stack, product)) {
            return true;
        }
    }
    return false;
}

/// 从一组容器里按严格匹配扣掉 `items` 件。
///
/// 凑不够就返回 false，调用方按快照把容器写回去（本函数自己不管补偿）。
/// 买入从商店箱扣、卖出从玩家背包扣，所以容器是参数而不是写死的那一个。
///
/// `sampleSlot` 非空时走**可卖顺序**（`saleableSlotOrder`：从最后一格往前、跳过样品槽）：
/// 样品槽永远不出售，于是牌子上写的商品始终有实物兜底，卖光其他格子也不会突然变成
/// “还没设商品”。卖出方向扣的是玩家背包，没有样品槽这个概念，传空即可。
///
/// `taken` 非空时把「扣下来的那几个堆」各复制一份塞进去：管理员店卖出方向的货要被**销毁**，
/// 而销毁之后再没有任何地方能读到它——SNBT 留档只能靠这一份副本（票 02）。
/// 买入方向传空：那些货是发给玩家的，不销毁，也没有留档可言。
bool takeMatchingItems(
    Container&              primary,
    Container*              secondary,
    int                     slots,
    ItemStack const&        product,
    int                     items,
    std::vector<ItemStack>* taken,
    std::optional<int>      sampleSlot
) {
    auto const merged    = mergedSlots(primary, secondary, slots);
    int        remaining = items;
    for (int const index : saleableSlotOrder(static_cast<int>(merged.size()), sampleSlot)) {
        if (remaining <= 0) {
            break;
        }
        MergedSlot const& at    = merged[static_cast<std::size_t>(index)];
        ItemStack const&  stack = at.container->getItem(at.slot);
        if (stack.isNull() || !sameShopItem(stack, product)) {
            continue;
        }
        // 先算再改：`removeItem` 之后 `stack` 的引用就不能再读了。
        int const take = std::min(static_cast<int>(stack.mCount), remaining);
        if (taken != nullptr) {
            ItemStack copy = stack;
            copy.set(take); // 被扣走的那部分才是「被销毁的物品堆」
            taken->push_back(std::move(copy));
        }
        at.container->removeItem(at.slot, take);
        remaining -= take;
    }
    return remaining <= 0;
}

/// 把 `items` 件商品塞进背包（买入）。任何一批塞不进去就返回 false（调用方按快照回写）。
///
/// 按最大叠加数分批：`ItemStack::mCount` 只有一个字节，一件一件地塞也不现实。
bool giveItemsToBackpack(Player& buyer, ItemStack const& product, int items) {
    Inventory& inventory = buyer.getInventory();
    int const  chunkMax  = std::max(1, static_cast<int>(product.getMaxStackSize()));

    int remaining = items;
    while (remaining > 0) {
        int const chunk = std::min(remaining, chunkMax);
        // 每一批都从模板复制（含 NBT / 附魔），不共享用户数据。
        ItemStack stack = product;
        stack.set(chunk);
        if (!inventory.add(stack)) {
            return false;
        }
        remaining -= chunk;
    }
    return true;
}

/// 把 `items` 件商品放进商店箱（卖出）。放不进去就返回 false（调用方按快照回写并退款）。
///
/// 双箱是两半、各自是一个 `Container`：`addItem` 只认一个容器，所以先放附着那一半、
/// 剩下的给另一半（与 `mergedSlots` 的顺序一致）。`addItem` 会把放不下的部分留在
/// `stack` 里，所以用「这一批还剩几件」算实际放进去多少，算错就走补偿——不猜。
bool putItemsIntoShopBox(AttachedContainer const& attached, ItemStack const& product, int items) {
    int const chunkMax  = std::max(1, static_cast<int>(product.getMaxStackSize()));
    int       remaining = items;
    while (remaining > 0) {
        int const chunk = std::min(remaining, chunkMax);
        // 每一批都从模板复制（含 NBT / 附魔），不共享用户数据。
        ItemStack stack = product;
        stack.set(chunk);

        for (Container* half : {attached.primary, attached.secondary}) {
            if (half == nullptr || stack.isNull() || stack.mCount == 0) {
                continue;
            }
            half->addItem(stack);
        }

        int const left   = stack.isNull() ? 0 : static_cast<int>(stack.mCount);
        int const placed = chunk - left;
        if (placed <= 0) {
            return false; // 一件也没放进去：容量算错了，交给快照回写与退款
        }
        remaining -= placed;
    }
    return true;
}

/// 一次交易里「钱」的那一半：谁付、谁收、多少。
///
/// 拆成两块是因为商店税（DESIGN §8.9）：`main` 是这次**真正转移的成交额**，
/// `tax` 是店主要承担的商店税（从付款方另收一笔销毁）。
/// 买入时 `main = 总额 - 税`，买家总支出仍是 `总额`；卖出时 `main = 总额`，
/// 玩家收到的仍是牌面金额，店主要额外再付一笔税。
/// 税额由 `planTrade` 算好（`TradePlan::tax`），这里不再乘一次税率。
struct Payment {
    std::string  payer;
    std::string  receiver;
    std::int64_t main = 0;
    std::int64_t tax  = 0;
};

/// 一次交易的付款信息（DESIGN §10.2 第 5 步 / §8.9）。**方向只在这里分叉一次**：
/// 买入是买家付给店那一侧（`总额 - 税`，税另付），卖出是店那一侧付给玩家（`总额`，税另付）。
///
/// 「店那一侧」由 `shopSideAccount` 决定：普通店是店主 XUID，管理员店是系统账户（§7.4）。
/// 买卖两个方向各写一份这个组合，早晚会有一边忘了换收款方或忘了减税。
Payment paymentFor(
    TradeDirection      direction,
    ShopInfoData const& info,
    std::string_view    ownerXuid,
    std::string_view    traderXuid,
    TradePlan const&    plan
) {
    std::string const shopSide = economy::shopSideAccount(info.adminShop, ownerXuid);
    if (direction == TradeDirection::Buy) {
        return {std::string{traderXuid}, shopSide, plan.total - plan.tax, plan.tax};
    }
    return {shopSide, std::string{traderXuid}, plan.total, plan.tax};
}

/// 把钱原路退回：从收钱方反向再走一次，金额就是已经移动过的 `main`。
///
/// 税不回退：它永远是最后一步（见 `chargeTax`），真正需要退款的时候它还没被收。
/// 成功返回空；失败返回原因，由调用方交给 `broken()` 统一记日志。
///
/// 这里**不**自己打日志：退款失败与补偿失败是同一次事故，日志只能留一条。
std::optional<Text> refund(economy::EconomyProvider& provider, Payment const& payment) {
    if (payment.main <= 0) {
        return std::nullopt;
    }
    auto const back = provider.transfer(payment.receiver, payment.payer, payment.main);
    if (back.ok()) {
        return std::nullopt;
    }
    return back.detail;
}

/// 一次交易的现场：补偿路径需要的东西全在这里。
///
/// 打包的原因很实际：写回物品要箱子、写回背包要快照、退款要付款信息、刷新要世界，
/// 这四样在两条失败路径上永远一起出现，散成参数就是每次补一个漏一个。
struct TradeFrame {
    BlockSource&              region;
    Player&                   trader; ///< 站在玩家这一侧的人：买入时是买家，卖出时是卖方
    AttachedContainer         container;
    TradeSnapshots            snapshots;
    economy::EconomyProvider& provider;
    Payment                   payment;
};

/// 钱已经动过之后才发现成不了交：按快照把物品写回去、刷给客户端，再退款。
///
/// 两种失败（取货不足、塞不进去）的收尾完全一样，只有文案不同，所以只写一份：
/// 补偿逻辑一旦分叉，早晚有一边漏掉一步。买卖两个方向也共用它——
/// 差别只在 `frame.payment` 的方向，退款跟着付款信息走。
///
/// `reason` 是流水里的短名（`no_stock` / `no_space` / `tax_failed`…），跟着这一笔走：
/// 退款成不成都不改变「为什么没卖成」。
TradeResult undoAndRefund(
    TradeFrame&        frame,
    bool               restoreBackpack,
    Text               message,
    std::string_view   reason,
    std::string const& freeze
) {
    frame.snapshots.restoreShopBox();
    if (restoreBackpack) {
        // 背包可能已经被 `add` 塞进去一部分（或已经被扣掉一部分），整组槽位一起写回
        // 并把客户端刷过来，否则玩家屏幕上会留下已经退回去的那几个物品（DESIGN §10.2 第 9 步）。
        frame.snapshots.backpack.restore();
        frame.trader.sendInventory(true);
    }
    refreshShopBox(frame.region, frame.container);

    // 免费店（`main == 0`）根本没动过钱，别在提示里编一个「钱已退回」。
    if (frame.payment.main <= 0) {
        return reject(
            text::sequence({std::move(message), text::keyed("shop.trade.no_deal_suffix")}),
            reason
        );
    }
    if (auto const failed = refund(frame.provider, frame.payment)) {
        // 退款也失败：`broken()` 会把流水的原因改成 `compensation_failed`（那才是真事故），
        // 原先的 `reason` 留在文案里给玩家看。
        return broken(
            freeze,
            text::sequence({
                std::move(message),
                text::keyed("shop.trade.refund_failed_suffix", ss::shop::Tone::Default, {*failed}),
            })
        );
    }
    return reject(
        text::sequence({std::move(message), text::keyed("shop.trade.refunded_suffix")}),
        reason
    );
}

/// 流水事实里「一开始就知道」的那一半：方向、牌子坐标、交易者。
///
/// 商店还没解析出来时（「这家商店已经不在了」这种最早失败的路径）流水只有这几项——
/// 但仍然能定位到「谁在哪块牌前买了什么方向」，比不记强。
TradeFacts basicTradeFacts(TradeDirection direction, Player& trader, Cell const& sign) {
    TradeFacts facts;
    facts.dir         = direction;
    facts.dimensionId = trader.getDimensionId();
    facts.shop        = sign;
    facts.playerXuid  = trader.getXuid();
    facts.playerName  = trader.getNameTag();
    // `backend` 也要填：spec 只允许 `legacy_money` / `scoreboard` / `null`，空串会让
    // 「这个后端上的失败率」没法算，而「牌没了」「冻结名单」这些早失败正是最需要看后端的时候。
    // 这里问一次 `economyBackend()`：它自己缓存解析结果（只真解析一次），而这一笔是**真的在尝试
    // 交易**，不是 `/shop status` 那种纯查询——后者才需要避免顺手去建记分板目标。
    facts.backend = std::string{economyBackend().name()};
    return facts;
}

/// 商店快照拿到之后补齐另一半：店主、商品、单价、后端。
///
/// 在这一刻取、而不是等 `finishTrade` 再去读世界：商品模板、店主名、单价都是**这一笔**的样子，
/// 交易走完之后箱子、牌子或在线名单可能已经变了。
/// `TradeStatus` 到流水三态的映射不在这里——那是 `finishTrade` 的事。
void fillTradeFacts(
    TradeFacts&                     facts,
    ShopSnapshot const&             shop,
    ShopConfig const&               config,
    ItemStack const&                product,
    std::int64_t                    unitPrice,
    economy::EconomyProvider const& provider
) {
    facts.ownerXuid = shop.ownerXuid;
    // 管理员店的「店主」是配置里那个名字（spec：`owner.name = adminShopName`），
    // 不是牌上记的创建者——`readSignOwner` 在 `PlayerInfo` 里查得到时会拿创建者的名字。
    facts.ownerName = shop.info.adminShop ? config.adminShopName : shop.info.ownerName;
    facts.admin     = shop.info.adminShop;
    facts.backend   = std::string{provider.name()};
    facts.itemId    = product.getTypeName();
    facts.itemAux   = static_cast<int>(product.getAuxValue());
    facts.itemName  = product.getName();
    facts.unitPrice = unitPrice;
}

/// 一次交易的全部前置条件（DESIGN §10.2 第 2、3 步的输入）。买卖共用这一个结构：
/// 表单要显示「最多能成交几份」，交易要判断「这批能不能成交」，两处必须是同一套数。
struct TradeContext {
    AttachedContainer         container;
    ItemStack                 product; ///< 商品模板的副本（箱子一被改动，槽位里的那份就失效了）
    /// 商品模板在合并顺序里的下标（= 样品槽）。买入扣货要跳过它（样品槽永不出售），
    /// 而“第几格”只有 `productSlotOf` 算得出来，所以不能到了扣货那一步再重新猜。
    int                       sampleSlot = 0;
    ShopSnapshot              shop;
    economy::EconomyProvider* provider = nullptr;
    TradeRequest              request;
    /// 这一次真的最多能成交几份（`maxTradeUnits` 的结果）。
    /// 与 `request.maxUnits` 不是一回事：那个是配置里的天花板（输入），这个是算完的结论。
    int unitsAvailable = 0;
};

/// 预校验的结果：`context` 为空时 `error` 已经写好给玩家看的原因。
struct PreparedTrade {
    std::optional<TradeContext> context;
    Text                        error;
    /// 同一个失败在流水里的短名（`error` 是玩家看的中文，两件事分开）。
    /// 走到这里的失败都是**世界状态**（商店没了、后端没起来……），所以走 `RejectReason`；
    /// 唯一例外是停业——那条原因由 `TradeRejection::ShopClosed` 统一给名字。
    std::string_view reason;
};

/// 买卖共用的前置检查（DESIGN §10.2 第 2 步）：牌子还是不是商店、能不能交易、商品是什么。
///
/// 两个方向只有三句话不同（不营业、没人能收/付钱、箱子空了看不出收什么），
/// 其余八项检查一字不差，所以只写一遍。失败时只填 `error`，**一分钱一件货都不动**。
PreparedTrade prepareCommon(
    TradeDirection    direction,
    BlockSource&      region,
    Cell const&       sign,
    Player&           trader,
    ShopConfig const& config
) {
    bool const selling = direction == TradeDirection::Sell;

    // 重新解析商店：不信任表单里那一份快照（DESIGN §10.4）。
    auto shop = readShop(region, toBlockPos(sign), config);
    if (!shop) {
        return {.error = text::keyed("shop.trade.shop_missing"), .reason = rejectReasonName(RejectReason::ShopMissing)};
    }
    if (shop->info.closed) {
        // 停业是整家店的属性，比任何上限（以及“这一侧不收/不卖”）都优先。
        //
        // 它与 `planTrade` 里那一条**不是可合并的重复**，不要当“死逻辑”删掉（已经有过一次审查提议）：
        // - 这里：提交路径的**用户可见闸口**。旧表单（店主在玩家打开份数表单之后才停业）
        //   走到这里时，箱子可能已经空了、后端可能不可用、店主可能已离线——那些原因都不如
        //   「这家店暂停营业」对得上号。
        // - `planTrade`：纯逻辑层的**权威判据**与单测缝，不靠「调用方已经拦过」这个前提。
        return {
            .error  = tradeRejectionMessage(direction, TradeRejection::ShopClosed),
            .reason = tradeRejectionReasonName(TradeRejection::ShopClosed),
        };
    }
    if (!unitPriceOf(shop->info, direction)) {
        return {
            .error  = selling ? text::keyed("shop.trade.not_buying") : text::keyed("shop.trade.not_selling"),
            .reason = rejectReasonName(RejectReason::ShopUnavailable)
        };
    }
    // 普通店两边都靠店主这一个账户：买入店主收钱、卖出店主付钱（ADR-0005）。
    // 管理员店的对手方是系统账户（§7.4），牌上记的 XUID 只是当时的创建者，所以两条都不适用：
    // 「读不到店主」不影响交易，OP 在管理员店里也不是店主（`shopFormRoleOf` 已按访客给按钮）。
    if (!shop->info.adminShop) {
        if (shop->ownerXuid.empty()) {
            return {
                .error  = selling ? text::keyed("shop.trade.owner_unreadable_pay") : text::keyed("shop.trade.owner_unreadable_charge"),
                .reason = rejectReasonName(RejectReason::OwnerUnknown),
            };
        }
        if (shop->ownerXuid == trader.getXuid()) {
            return {.error = text::keyed("shop.trade.self_trade"), .reason = rejectReasonName(RejectReason::SelfTrade)};
        }
    }
    if (isFrozen(trader.getXuid(), trader.getDimensionId(), sign)) {
        // 上一次补偿失败还挂着：这次的「钱货一致」没人能保证，别再放行
        // （DESIGN §10.2 第 8 步的「冻结该玩家本次操作，不自动重试」）。
        // 被冻结的后续提交是 `fail` + `frozen_pair`，**不是**又一条 `frozen`：
        // 事故只发生了一次，记成两次会让服主去查两次。
        return {
            .error  = text::keyed("shop.trade.frozen_pair"),
            .reason = rejectReasonName(RejectReason::FrozenPair),
        };
    }
    if (config.ignoreCreativeMode && trader.isCreative()) {
        // 创造模式玩家不参与经济（DESIGN §10.4）
        return {.error = text::keyed("shop.trade.creative_mode"), .reason = rejectReasonName(RejectReason::CreativeMode)};
    }

    // 余额与能力都要问后端，只问一次（下面 `context.provider` 用的就是这一个）。
    auto& provider = economyBackend();
    if (!provider.available()) {
        return {.error = noEconomyReason(), .reason = rejectReasonName(RejectReason::NoBackend)};
    }
    if (shop->info.adminShop && !provider.supportsSystemAccount()) {
        // 管理员店的资金侧不设上限靠的就是系统账户（§7.4）：后端声明不了这项能力时
        // 两个方向都必然失败，不如提前说清楚（与点牌时藏按钮同一句原因）。
        return {.error = noSystemAccountReason(), .reason = rejectReasonName(RejectReason::NoSystemAccount)};
    }

    auto attached = attachedContainerOf(region, toBlockPos(sign), config.shopContainers);
    if (!attached.ok()) {
        return {.error = text::keyed("shop.trade.box_missing"), .reason = rejectReasonName(RejectReason::BoxMissing)};
    }
    auto const product = productSlotOf(*attached.container);
    if (!product) {
        // 商品权威是商店箱里第一个非空槽位（DESIGN §5.3）：箱子空了就没人知道这家店收什么，
        // 也没东西可卖。牌面第 4 行只有显示名，拿它反查物品会造出第二个真相源，P0 不做。
        //
        // 管理员店此时不是「缺货」而是「还没设商品」（票 03）：两条原因分开说，
        // 因为前者等补货、后者只能由 OP 往模板箱里放一件样品。
        if (shop->info.adminShop) {
            return {
                .error  = unavailableReasonForEmptyBox(/* adminShop=*/true),
                .reason = rejectReasonName(RejectReason::NoProduct),
            };
        }
        return {
            .error  = selling ? text::keyed("shop.trade.chest_empty_unknown")
                              : tradeRejectionMessage(direction, TradeRejection::NotEnoughGoods),
            .reason = tradeRejectionReasonName(TradeRejection::NotEnoughGoods),
        };
    }
    // 收纳袋一律不能当商品，装在潜影盒里也一样：内容在服务端的动态容器注册表里，
    // 读不出来（`BundleRules.h` / `productHoldsBundle`）。
    // 与禁售不同，这一条**不分店的性质**：读不到内容是能力限制，管理员店也一样。
    if (productHoldsBundle(*product->stack)) {
        return {
            .error  = bundleProductReason(),
            .reason = rejectReasonName(RejectReason::BundleProduct),
        };
    }
    // 禁售物品：玩家开的店不能卖被服主禁售的东西（含装在盒子里嵌套的）。
    // 表单那边由 `readShop` 报同一句原因；旧表单里没这条、玩家又硬提交时，这里就是唯一能拦住它的地方。
    // **管理员店不受影响**：那是服主自己摆的货架，样品箱与凭空造货都由他决定，
    // 禁又禁不掉（他本来就能 `/give`），只会把自己想卖的东西卡住。
    if (!shop->info.adminShop && productBanned(*product->stack, currentBanList())) {
        return {
            .error  = bannedItemReason(),
            .reason = rejectReasonName(RejectReason::BannedItem),
        };
    }
    // 容器类商品的盒内必须整齐（同物品、同数量）：不整齐的盒子不能当商品。
    // 表单那边由 `readShop` 报同一句话（`mixedContainerContentsReason`）——
    // 旧表单里没这条原因、玩家又硬提交时，这里就是唯一能拦住它的地方。
    if (containerProductMixed(*product->stack)) {
        return {
            .error  = mixedContainerContentsReason(),
            .reason = rejectReasonName(RejectReason::MixedContents),
        };
    }

    TradeContext context;
    context.container = *attached.container;
    // 商品模板当场拷一份：`product->stack` 指向箱子里的槽位，扣货之后那个引用就失效了。
    context.product    = *product->stack;
    context.sampleSlot = product->index;
    context.shop       = std::move(*shop);
    context.provider   = &provider;

    context.request.amountPerUnit = context.shop.info.amountPerUnit;
    context.request.maxUnits      = config.maxShopAmount;
    context.request.allowPartial  = config.allowPartialTransactions;
    context.request.closed        = context.shop.info.closed;
    // 注：`prepareCommon` 上面已经把停业的店拦了，所以这份 `closed` 在生产路径上恒为 false。
    // 仍然装进去，是因为 `planTrade` 是纯逻辑层的权威判据：它不该依赖「调用方已经拦过」这个前提
    // （单测、以后新加的调用路径都靠它）。
    // 管理员店没有「店主此刻在不在线」这回事：对手方是系统账户（§8.5 表格的「同左」）。
    context.request.ownerOnline = context.shop.info.adminShop || onlinePlayer(context.shop.ownerXuid) != nullptr;
    context.request.backendSupportsOffline = context.provider->supportsOffline();
    context.request.requireOwnerOnline     = config.economy.requireOwnerOnline;
    // 商店税（DESIGN §8.9）：税从这笔生意里抽，两个方向都要算；收不走（没有系统账户）时整笔不算；
    // 「压在谁身上」由方向决定（买入是店主少收，卖出是店主另付）。两个方向加后端能力、
    // 以及「管理员店没有可抽的成交额」这几条判断由纯函数决定——这里不再自己写 `direction == Sell`。
    auto const tax = tradeTaxPlan(
        direction,
        config.tax.shopTaxRate,
        context.provider->supportsMoneySink(),
        context.shop.info.adminShop
    );
    context.request.taxRate    = tax.taxRate;
    context.request.taxOnPayer = tax.taxOnPayer;

    return {.context = std::move(context), .error = {}, .reason = {}};
}

/// 买入方向的预校验：在共用检查之上装买入的角色（货在商店箱，收货方是玩家背包，
/// 付款方是买家）并算出份数上限（DESIGN §10.3）。
PreparedTrade prepareBuy(BlockSource& region, Cell const& sign, Player& buyer, ShopConfig const& config) {
    auto prepared = prepareCommon(TradeDirection::Buy, region, sign, buyer, config);
    if (!prepared.context) {
        return prepared;
    }
    auto& context = *prepared.context;

    // 买家必在线，读不到他的余额说明后端坏了——这时候不能当成「余额 0」继续算份数。
    // 免费店（B 0）不产生账户操作，余额读不到也不影响成交。
    auto const funds = context.provider->get(buyer.getXuid());
    if (!funds && *context.shop.info.buyPrice > 0) {
        return {.error = playerFundsUnknownReason(), .reason = rejectReasonName(RejectReason::PlayerFundsUnknown)};
    }

    context.request.unitPrice  = *context.shop.info.buyPrice;
    context.request.payerFunds = funds.value_or(0);
    // 份数表单要显示余额（DESIGN §10.1 第 3 步），就用刚读到的这个数，别再读一次。
    context.shop.info.balance  = funds;
    context.request.goodsUnits = context.shop.info.stockUnits;
    context.request.roomUnits  = backpackUnits(buyer.getInventory(), context.product, context.shop.info.amountPerUnit);

    // 上限 = maxShopAmount、可成交份数、余额、背包取最小（DESIGN §10.1 第 3 步）。
    // 按 `payerTaxRate` 传：买入方向是 0（税从店主的收入里扣，买家只付牌面总额），
    // 否则会把买家的份数上限按「总额 + 税」算，钱够的买家会被判「钱不够」。
    context.unitsAvailable = maxTradeUnits(
        context.request.maxUnits,
        context.request.goodsUnits,
        context.request.roomUnits,
        context.request.unitPrice,
        context.request.payerFunds,
        payerTaxRate(context.request)
    );
    return prepared;
}

/// 卖出方向的预校验：角色反过来（货在玩家背包，收货方是商店箱，付款方是店主）。
PreparedTrade prepareSell(BlockSource& region, Cell const& sign, Player& seller, ShopConfig const& config) {
    auto prepared = prepareCommon(TradeDirection::Sell, region, sign, seller, config);
    if (!prepared.context) {
        return prepared;
    }
    auto& context = *prepared.context;

    // 卖出是店主付钱给玩家：店主的余额是份数上限之一，必须**读得到**。
    // 读不到（账户不可记账）与「余额是 0」是两件事——当成 0 会对着一个有钱的店主
    // 报「店主资金不足」，事故无从查起（DESIGN §8.2）。
    // 免费店（S 0）不产生账户操作，店主余额读不到也不算错。
    // 管理员店没有店主余额这回事：资金侧不设上限（§7.4 / GLOSSARY）。
    bool const adminShop = context.shop.info.adminShop;

    std::optional<std::int64_t> ownerFunds;
    if (!adminShop) {
        ownerFunds = context.provider->get(context.shop.ownerXuid);
        if (!ownerFunds && *context.shop.info.sellPrice > 0) {
            // 不支持离线记账的后端（记分板）在店主离线时本来就读不到余额：
            // 真正的原因是「店主不在线」，报那一句才对得上号（DESIGN §8.5）。
            // 判据与表单藏按钮、`planTrade` 完全共用一个函数。
            if (economy::ownerUnreachable(
                    context.request.ownerOnline,
                    context.request.backendSupportsOffline,
                    context.request.requireOwnerOnline
                )) {
                return {
                    .error  = ownerOfflineReason(),
                    .reason = tradeRejectionReasonName(TradeRejection::OwnerOffline)
                };
            }
            return {.error = ownerFundsUnknownReason(), .reason = rejectReasonName(RejectReason::OwnerFundsUnknown)};
        }
    }

    context.request.unitPrice  = *context.shop.info.sellPrice;
    context.request.payerFunds = economy::shopSideFunds(adminShop, ownerFunds.value_or(0));
    // 份数与件数都要：件数为 0 才是「根本没有」，份数为 0 但件数不为 0 只是「不够一份」。
    auto const strictItems =
        countMatchingItems(seller.getInventory(), nullptr, seller.getInventory().getContainerSize(), context.product);
    auto const strictCount = static_cast<int>(std::min<std::int64_t>(strictItems, std::numeric_limits<int>::max()));
    // 卖出方向的「货」是玩家背包里的实物，**永远有限**：把它当无限 = 凭空给玩家复制物品。
    // 无限只属于管理员店那一侧的模板箱（买入方向，见 `readShop` 与 `buyUnits`）。
    int const heldUnits = unitsFromItems(strictCount, context.shop.info.amountPerUnit);
    // 管理员店收进来的货是**销毁**的（票 02）：箱子不是收货方，所以「还能装几份」不存在。
    // 普通店照旧算商店箱（双箱按合并顺序）还能装几份。
    UnitCount const boxRoomUnits =
        adminShop
            ? UnitCount::makeUnlimited()
            : UnitCount::makeFinite(shopBoxUnits(context.container, context.product, context.shop.info.amountPerUnit));
    context.request.goodsUnits = UnitCount::makeFinite(heldUnits);
    context.request.roomUnits  = boxRoomUnits;

    // 「我手里根本没有」与「我手里有钻石剑但不是这一把」得分开说（DESIGN §5.3 的严格同种）。
    // 只有严格同种一件都没有时才怀疑「货不对」；有 63 个差一个满一份的，原因就是不够一份。
    if (strictCount == 0 && holdsSimilarItem(seller.getInventory(), context.product)) {
        // 「货不对」不是「没货」：`planTrade` 会把它报成 `NotEnoughGoods`，但流水里
        // 这两种原因必须分得开（一个该补货、一个该看自己手里的附魔）。
        return {
            .error  = text::keyed("shop.trade.item_mismatch"),
            .reason = rejectReasonName(RejectReason::ItemMismatch),
        };
    }

    // 卖出表单要把「你手里有几份」「商店箱还能装几份」「店主余额」都摆出来
    // （DESIGN §10.1 第 3 步：表单要显示上限是怎么来的，不能只说“受它们限制”）。
    // 玩家自己的余额读不到不算错误：钱那一步会在扣款前再确认一次两边账户（见 `moveMoney`）。
    context.shop.info.heldUnits    = heldUnits;
    context.shop.info.boxRoomUnits = boxRoomUnits;
    // 管理员店的「店主余额」不编一个数字：资金侧不设上限，它不是一个可读的账户。
    context.shop.info.ownerBalance = ownerFunds;
    context.shop.info.balance      = context.provider->get(seller.getXuid());

    // 上限 = maxShopAmount、手里有的份数、箱子还能装的份数、店主余额取最小（§10.3）。
    // 店主余额带税算（`payerTaxRate` = 税率）：商店税是店主掏的，多卖一份就可能付不起那笔税。
    context.unitsAvailable = maxTradeUnits(
        context.request.maxUnits,
        context.request.goodsUnits,
        context.request.roomUnits,
        context.request.unitPrice,
        context.request.payerFunds,
        payerTaxRate(context.request)
    );
    return prepared;
}

/// 把「重新解析玩家 → 干正事」投到服务端线程。
///
/// 表单回调在哪个线程跑是不确定的（DESIGN §14 待验），所以回调里一个世界状态都不碰：
/// 只捕获「谁的 XUID、在哪个维度、哪块牌」，执行时再重新解析玩家与商店。
///
/// 玩家可能在排队期间下线或换维度，两种情况都必须留下痕迹：一行日志、或**能发给玩家的一句提示**。
/// 曾经这两条都是静默 `return`，玩家的反馈是「点了购买没有任何反应」——排障时既无日志也无提示。
template <class Fn>
void onServerThread(Player& player, Cell const& sign, bool useCooldown, Fn&& fn) {
    std::string const xuid      = player.getXuid();
    int const         dimension = player.getDimensionId();

    ll::thread::ServerThreadExecutor::getDefault().execute([xuid, dimension, sign, useCooldown, fn] {
        auto& logger       = Shop::getInstance().getSelf().getLogger();
        auto const& config = Shop::getInstance().config();
        if (!config) {
            // 模组正在关闭（`/ll reload`）：没有任何配置能拿来执行，只能留一行痕迹
            logger.debug(ui::msg("shop.log.form.dropped_no_config", xuid));
            return;
        }
        Player* resolved = onlinePlayer(xuid);
        if (resolved == nullptr) {
            // 下线了，消息发不出去：至少让日志能对上「玩家说点了没反应」
            logger.debug(ui::msg("shop.log.form.dropped_offline", xuid));
            return;
        }
        if (resolved->getDimensionId() != dimension) {
            // 换维度：那块牌子已经不在他眼前，继续执行会操作一个他看不到的商店。
            // 必须说一句——曾经这里是静默 `return`，玩家只看到「点了没反应」。
            ui::send(*resolved, ui::Tone::Warning, "shop.trade.wrong_dimension");
            return;
        }
        if (useCooldown && gSubmitGate && !gSubmitGate->allow(xuid, dimension, sign, monotonicNowMs())) {
            // 250ms 内重复提交：忽略（DESIGN §10.4）。这里**故意不**给玩家消息：
            // 连点两下只该成交一笔，弹一句「太快了」反而会让人以为第一笔没成功。
            // 但它不是完全无声的：debug 打开时留一行，排障时能对上。
            if (shopDebugEnabled()) {
                shopDebugEventThrottled(
                    "submit_cooldown",
                    {dbgField("player", xuid), dbgField("pos", dbgPos(dimension, sign.x, sign.y, sign.z))}
                );
            }
            return;
        }
        fn(*resolved, sign, *config);
    });
}

/// 表单值与重算值对不上就先弹确认（DESIGN §10.3），什么都不动。
///
/// 三个都要比：店主改了「一份的件数」时份数与金额可能都没变，只有件数变了。
/// 确认过也照比——确认后状态又变就得再点一次，绝不静默按新值成交。买卖共用。
std::optional<TradeResult> confirmIfChanged(
    TradeDirection         direction,
    ShopInfoData const&    info,
    TradePlan const&       plan,
    TradeSubmission const& submission
) {
    bool const changedUnits = plan.units != submission.units;
    bool const changedItems = submission.expectedItems && plan.items != *submission.expectedItems;
    bool const changedTotal = submission.expectedTotal && plan.total != *submission.expectedTotal;
    if (!changedUnits && !changedItems && !changedTotal) {
        return std::nullopt;
    }
    ChangeReason const reason = changedUnits ? ChangeReason::Units
                              : changedItems ? ChangeReason::Items
                                             : ChangeReason::Price;
    return TradeResult{
        .status  = TradeStatus::NeedsConfirm,
        .plan    = plan,
        .message = text::block(unitsConfirmContent(direction, info, plan, reason)),
    };
}

/// 钱的那一步（买卖共用）。成功返回空；否则是已经成形、直接发给玩家的结果。
///
/// 扣款之前先确认两边账户都能记账：后端的 `transfer` 是「先扣款再入账，入账失败就把钱
/// 加回去」，而加回去本身也可能失败（那就成了 `Broken`）。这一步失败时两边余额一分未动
/// （DESIGN §10.2 第 2 步）。
std::optional<TradeResult> moveMoney(
    TradeDirection            direction,
    economy::EconomyProvider& provider,
    Payment const&            payment,
    std::string const&        freeze
) {
    // 免费店（`main == 0`）不产生任何账户操作，账户读不到也不该拦住成交。
    // （税按总额算，总额为 0 时税也必然是 0。）
    if (payment.main <= 0) {
        return std::nullopt;
    }
    if (!accountsRecordable(provider, payment.payer, payment.receiver)) {
        // 有一头是系统账户（管理员店）时，可读的只有玩家那一侧：读不到就是玩家账户的问题，
        // 不能报成「后端没有系统账户」——那项能力在预校验里已经判过了（DESIGN §8.2）。
        if (payment.payer.empty() || payment.receiver.empty()) {
            return reject(playerFundsUnknownReason(), RejectReason::PlayerFundsUnknown);
        }
        // 两个方向各有一个账户在预校验里已经查过（买入查买家、卖出查店主），
        // 所以走到这里的读不到，是另一头出了问题。
        return reject(
            direction == TradeDirection::Buy ? ownerFundsUnknownReason() : playerFundsUnknownReason(),
            direction == TradeDirection::Buy ? RejectReason::OwnerFundsUnknown : RejectReason::PlayerFundsUnknown
        );
    }
    auto const paid = provider.transfer(payment.payer, payment.receiver, payment.main);
    if (paid.ok()) {
        return std::nullopt;
    }
    if (paid.broken()) {
        return broken(
            freeze,
            text::keyed("shop.trade.payment_broken", ss::shop::Tone::Default, {paid.detail})
        );
    }
    // 买入是玩家付钱，卖出是店主付钱——失败原因得归到对的人头上。
    // 管理员店的卖出方向付款方是系统账户（§7.4），不叫「店主」。
    // 卖出方向的付款方是店主；买入方向是玩家自己，不必点名。
    auto const payer = payment.payer.empty()
                         ? text::keyed("shop.trade.payer.system")
                         : (direction == TradeDirection::Buy ? Text{} : text::keyed("shop.form.owner"));
    return reject(
        text::keyed("shop.trade.payment_failed", ss::shop::Tone::Default, {payer, paid.detail}),
        RejectReason::PaymentFailed
    );
}

/// 收商店税时的失败（DESIGN §8.9）。
struct TaxFailure {
    bool broken = false; ///< 钱可能已经动过且没还原：按 §10.2 第 8 步升级为严重错误
    Text detail;
};

/// 第 5 步的后半段：商店税（DESIGN §8.9）。成功（或本来就没有税）返回空。
///
/// **税放在物品那一步之后**，这一点偏离了 §8.9 字面上的「主交易后紧接着收税」，理由是
/// 补偿：税一旦销毁就再也补不回来。若先收税、后面移物品才失败，退款只能退 `总额 - 税`，
/// 买家凭空少一笔税，§10.2 的「任一步失败两边余额不变」就不成立了。
/// 把收税挪到最后，任何失败点都只涉及已经移动过的 `payment.main`，补偿永远精确。
///
/// 税为 0 完全跳过第二笔（LegacyMoney 的 `val == 0` 是合法的，会白写一条 0 元流水）；
/// 后端没有系统账户（记分板）时也跳过：店主本来就只收到 `总额 - 税`，
/// 那笔税从来没记进任何人的账，效果一样。
std::optional<TaxFailure> chargeTax(economy::EconomyProvider& provider, Payment const& payment) {
    if (payment.tax <= 0 || !provider.supportsMoneySink()) {
        return std::nullopt;
    }
    auto const collected = provider.transfer(payment.payer, economy::Account{}, payment.tax);
    if (collected.ok()) {
        return std::nullopt;
    }
    return TaxFailure{collected.broken(), collected.detail};
}

/// 收税的收尾（买卖共用）：税没收到就把物品与主交易一起退回。
///
/// 两个方向的差别只有「玩家这一侧的背包要不要写回」（收税发生在物品之后，两边都动过），
/// 所以只需要一个参数，不必把整段补偿路径抄两遍——补偿一旦分叉，早晚有一边漏一步。
std::optional<TradeResult> settleTax(TradeFrame& frame, std::string const& freeze) {
    auto const failed = chargeTax(frame.provider, frame.payment);
    if (!failed) {
        return std::nullopt;
    }
    if (failed->broken) {
        return broken(freeze, taxBrokenReason(failed->detail));
    }
    return undoAndRefund(
        frame,
        /*restoreBackpack=*/true,
        taxChargeFailureReason(failed->detail),
        rejectReasonName(RejectReason::TaxFailed),
        freeze
    );
}

/// 「点买/卖 → 预校验 → 弹份数表单」的同一套外壳：两个方向只差 `prepare`。
///
/// 请求份数取默认的 1，正是为了让「店主离线」这类与份数无关的拒绝也能在弹表单之前命中。
template <class Prepare>
void requestUnitsForm(
    Player&           player,
    Cell const&       sign,
    TradeDirection    direction,
    ShopConfig const& config,
    Prepare&&         prepare
) {
    auto prepared = prepare(player.getDimensionBlockSource(), sign, player, config);
    if (!prepared.context) {
        // 店没了 / 权限变了 / 箱子里货不对：都是「这次不行」，红色。
        ui::send(player, ui::Tone::Failure, prepared.error);
        return;
    }
    auto const& context = *prepared.context;
    auto const  oneUnit = planTrade(context.request);

    if (!oneUnit.ok()) {
        if (oneUnit.rejection == TradeRejection::InvalidRequest) {
            // 份数/份价/每份件数在开店时就校验过，活到这里就是商店或配置坏了，
            // 别当成玩家的输入错误报出去。
            Shop::getInstance().getSelf().getLogger().error(ui::msg("shop.log.trade.inconsistent", sign.x, sign.y, sign.z)
            );
            ui::send(player, ui::Tone::Failure, "shop.trade.inconsistent");
            return;
        }
        ui::send(player, ui::Tone::Failure, tradeRejectionMessage(direction, oneUnit.rejection));
        return;
    }

    Shop::getInstance().getSelf().getLogger().debug(ui::msg(
            "shop.log.trade.form_opened",
            player.getXuid(),
            ui::plain(text::keyed(std::string{directionVerb(direction)})),
            context.unitsAvailable
        )
    );
    openTradeForm(player, sign, direction, context.shop.info, context.unitsAvailable);
}

/// `TradeStatus` → 流水三态（spec D1）。**只在这里映射**：`TradeStatus` 多一个
/// `NeedsConfirm`，而流水只要终态，所以它返回空表示「这一笔没有可写的流水」。
/// 纯逻辑层不认识 `TradeStatus`（`TradeLogResult` 是另一套枚举），这条知识属于 mod 一侧。
std::optional<TradeLogResult> tradeLogResultOf(TradeStatus status) {
    switch (status) {
    case TradeStatus::Done:
        return TradeLogResult::Ok;
    case TradeStatus::Rejected:
        return TradeLogResult::Fail;
    case TradeStatus::Broken:
        return TradeLogResult::Frozen;
    case TradeStatus::NeedsConfirm:
    default:
        return std::nullopt;
    }
}

/// 提交之后的三件事：要确认就弹确认表单、成交就记一条日志、其余情况把消息发给玩家。
/// 买卖共用一份，免得两边各写一遍「忘了记日志」。
void finishTrade(Player& player, Cell const& sign, TradeDirection direction, TradeResult const& result) {
    // 唯一漏斗：两个方向的每一种终态都从这里出去，所以调试与流水将来都挂在这一处。
    if (shopDebugEnabled()) {
        // 终态的名字只从 `tradeLogResultOf` 取：调试输出与流水不能对同一笔交易说两种结论。
        // `NeedsConfirm` 不在三态里（它没有流水），调试这边叫 `confirm`。
        auto const       logged  = tradeLogResultOf(result.status);
        std::string_view outcome = logged ? tradeLogResultName(*logged) : std::string_view{"confirm"};
        shopDebugEvent(
            "trade_result",
            {dbgField("player", player.getXuid()),
             dbgField("pos", dbgPos(player.getDimensionBlockSource().getDimensionId(), sign.x, sign.y, sign.z)),
             dbgField("dir", direction == TradeDirection::Buy ? "buy" : "sell"),
             dbgField("units", (long long)result.plan.units),
             dbgField("items", (long long)result.plan.items),
             dbgField("total", (long long)result.plan.total),
             dbgField("tax", (long long)result.plan.tax),
             dbgField("outcome", outcome),
             dbgField("reason", result.done() ? std::string{} : text::render(result.message))}
        );
    }

    if (result.needsConfirm()) {
        // 实际值与玩家看到的不一样：先把真实份数与金额摆出来，确认后才执行。
        openTradeConfirmForm(player, sign, direction, result.plan, result.message);
        return;
    }
    // 成交用绿色，被拒用红色：`Broken` 也有消息，但它同时会走上面那条 fatal 日志，
    // 玩家这边不该看到「绿的成功」。
    ui::send(player, result.done() ? ui::Tone::Success : ui::Tone::Failure, result.message);
    // 成交流水（`p1-transaction-log` spec D1）：**唯一**接线点就是这里，两个方向每一种终态
    // 都从这里出去。三态映射也只写在这里：
    // - `Done` → `ok`；`Rejected` → `fail`；`Broken` → `frozen`（与上面那条 fatal 日志成对）；
    // - `NeedsConfirm` → **不写**：它不是终态，玩家可能直接关掉确认表单，写了就是噪音，
    //   而且会被读成「失败」。
    // 写失败只 warn：交易已经完成，流水不是它的一部分。
    if (result.logFacts) {
        if (auto const logged = tradeLogResultOf(result.status)) {
            writeTradeRecord(
                composeTradeRecord(*result.logFacts, result.plan, *logged, result.logReason, nowIso8601Local())
            );
        }
    }
    if (result.done()) {
        // 管理员店卖出销毁的货留档（`p1-transaction-log` spec D5）。写失败只 warn：
        // 交易已经成交，留档不是它的一部分——不许因为磁盘满了把一笔生意判成失败。
        writeDiscardRecords(result.discarded);
        // 两个分支各传各的参数：`done` 的模板没有「商店税」这一格，多传一个实参
        // 虽然会被 `fmt` 忽略，但调用点与词表就对不上了（测试按调用点逐个核对个数）。
        auto const verb = ui::plain(text::keyed(std::string{directionVerb(direction)}));
        if (result.plan.tax > 0) {
            Shop::getInstance().getSelf().getLogger().debug(ui::msg(
                "shop.log.trade.done_with_tax",
                player.getXuid(),
                sign.x,
                sign.y,
                sign.z,
                verb,
                result.plan.units,
                result.plan.total,
                result.plan.tax
            ));
        } else {
            Shop::getInstance().getSelf().getLogger().debug(ui::msg(
                "shop.log.trade.done",
                player.getXuid(),
                sign.x,
                sign.y,
                sign.z,
                verb,
                result.plan.units,
                result.plan.total
            ));
        }
    }
}

/// 「提交 → 执行 → 收尾」的同一套外壳：两个方向只差 `trade`（`buyUnits` / `sellUnits`）。
template <class Trade>
void submitTrade(
    Player&                player,
    Cell const&            sign,
    TradeDirection         direction,
    TradeSubmission const& submission,
    Trade&&                trade
) {
    onServerThread(
        player,
        sign,
        // 确认表单的「确认」紧接着第一次提交，再走一遍冷却会被静默吃掉。
        /*useCooldown=*/!submission.confirmed,
        [submission, direction, trade](Player& trader, Cell const& shopSign, ShopConfig const& config) {
            auto const result = trade(trader.getDimensionBlockSource(), shopSign, trader, config, submission);
            finishTrade(trader, shopSign, direction, result);
        }
    );
}

} // namespace

TradeResult buyUnits(
    BlockSource&           region,
    Cell const&            sign,
    Player&                buyer,
    ShopConfig const&      config,
    TradeSubmission const& submission
) {
    // 流水事实：一开始只有「谁、在哪块牌、哪个方向」——牌没了（`prepareBuy` 就失败）时
    // 这三项也够定位；商店解析出来之后再补齐店主、商品、单价与后端。
    TradeFacts facts    = basicTradeFacts(TradeDirection::Buy, buyer, sign);
    auto       prepared = prepareBuy(region, sign, buyer, config);
    if (!prepared.context) {
        return withFacts(reject(std::move(prepared.error), prepared.reason), facts);
    }
    auto context = std::move(prepared.context);
    fillTradeFacts(facts, context->shop, config, context->product, context->request.unitPrice, *context->provider);

    // 第 3 步：算可成交份数（部分成交开关在这里生效，DESIGN §10.3）。
    TradeRequest request = context->request;
    request.requested    = submission.units;

    auto const decision = planTrade(request);
    if (!decision.ok()) {
        return withFacts(
            reject(tradeRejectionMessage(TradeDirection::Buy, decision.rejection), decision.rejection),
            facts
        );
    }
    TradePlan const plan = *decision.plan;

    // 第 4 步之前先确认玩家看到的就是要执行的：对不上就什么都不做，先弹确认表单。
    if (auto confirm = confirmIfChanged(
            TradeDirection::Buy,
            context->shop.info,
            plan,
            submission
        )) {
        // `NeedsConfirm` 不是终态，不写流水：连事实也不必带上（`finishTrade` 不会用它）。
        return std::move(*confirm);
    }

    auto&               provider = *context->provider;
    ShopInfoData const& info     = context->shop.info;
    // 买入：买家付钱、店那一侧收钱。商店税从店主的收入里扣（店主承担），买家总支出仍是 `total`：
    // 主交易转 `total - tax`，税那一笔再由买家付给系统（DESIGN §8.9）。
    // `plan.tax` 为 0（没开税、后端收不走、或管理员店）时两者相等，第二笔整步跳过。
    Payment const     payment = paymentFor(TradeDirection::Buy, info, context->shop.ownerXuid, buyer.getXuid(), plan);
    std::string const freeze  = freezeKey(buyer.getXuid(), region.getDimensionId(), sign);

    // 第 4 步：快照。物品与余额不在同一个事务里，失败时只能靠它反向写回。
    // 主交易金额 = 玩家与店那一侧之间真正移动的那一笔（买入带税时是 `总额 - 税`）。
    facts.gross = payment.main;

    TradeFrame frame{
        region,
        buyer,
        context->container,
        TradeSnapshots{context->container, buyer.getInventory()},
        provider,
        payment
    };

    // 第 5 步：先扣钱。金额接口的失败语义最干净（返回 false 且余额一分不动），
    // 而物品搬运可能部分成功，所以钱一定要走在货前面（DESIGN §10.2 第 5 步）。
    if (auto money = moveMoney(TradeDirection::Buy, provider, payment, freeze)) {
        return withFacts(std::move(*money), facts);
    }

    // 第 6 步：先从箱子取货、再塞进背包。
    //
    // 这个顺序是关键：中间那一刻商品只在一处（箱子已经少了、背包还没多），
    // 不存在「同一件货同时在箱子和背包里」的复制窗口；反过来先塞背包再取箱子，
    // 中间那一刻就是凭空多出来的一份。
    //
    // 管理员店是**模板箱**：箱子里的那一件只用来定「卖什么」，货是凭空发出去的，
    // 所以这一整步要跳过——样品槽必须前后逐字段相同（spec D3 / 票 01）。
    if (!info.adminShop
        && !takeMatchingItems(
            *context->container.primary,
            context->container.secondary,
            context->container.ref.size,
            context->product,
            plan.items,
            /*taken=*/nullptr,
            /*sampleSlot=*/context->sampleSlot
        )) {
        return withFacts(
            undoAndRefund(
                frame,
                /*restoreBackpack=*/false,
                tradeRejectionMessage(TradeDirection::Buy, TradeRejection::NotEnoughGoods),
                tradeRejectionReasonName(TradeRejection::NotEnoughGoods),
                freeze
            ),
            facts
        );
    }

    if (!giveItemsToBackpack(buyer, context->product, plan.items)) {
        return withFacts(
            undoAndRefund(
                frame,
                /*restoreBackpack=*/true,
                tradeRejectionMessage(TradeDirection::Buy, TradeRejection::NoRoomToReceive),
                tradeRejectionReasonName(TradeRejection::NoRoomToReceive),
                freeze
            ),
            facts
        );
    }

    // 第 5 步的后半段：收商店税（DESIGN §8.9）。走到这里物品已经落进背包，税再失败
    // 就把物品与主交易一起退回去——税在这一步之前一分都还没销毁，补偿因此是精确的。
    if (auto const taxed = settleTax(frame, freeze)) {
        return withFacts(std::move(*taxed), facts);
    }

    // 第 9 步刷新：背包推给客户端，商店箱（双箱两半）各自 refresh 一次
    // （DESIGN §7.3「分别 setItem 并各自 refresh」/ §10.2 第 9 步）。
    // 管理员店的箱子一个字都没动，不必把它整箱重推一遍（白白让旁边的人看到一次假变化）。
    buyer.sendInventory(true);
    if (!info.adminShop) {
        refreshShopBox(region, context->container);
    }

    Text message = text::keyed("shop.trade.bought", ss::shop::Tone::Default, {
        text::lit(std::to_string(plan.units)),
        text::lit(info.itemName),
        text::lit(std::to_string(plan.items)),
    });
    if (plan.total > 0) {
        message = text::sequence({
            std::move(message),
            text::keyed("shop.trade.paid_suffix", ss::shop::Tone::Default, {
                text::lit(moneyText(info.currencySymbol, plan.total)),
            }),
        });
    }
    if (plan.units < submission.units) {
        // 部分成交：如实说清楚为什么只给了这么多（DESIGN §10.3）。
        // 管理员店的库存是无限的，别把「商店箱里的货」列成一个原因——它不存在。
        message = text::sequence({
            std::move(message),
            text::keyed(
                info.adminShop ? "shop.trade.partial_player" : "shop.trade.partial_stock",
                ss::shop::Tone::Default,
                {text::lit(std::to_string(plan.units))}
            ),
        });
    }
    // 成交的事实已经在 `fillTradeFacts` 里取好（那是这一笔的样子），结论与 `ts` 由
    // `finishTrade` 补：它才知道终态，也只有一处需要知道「哪种终态写流水」。
    return withFacts(TradeResult{.status = TradeStatus::Done, .plan = plan, .message = std::move(message)}, facts);
}

TradeResult sellUnits(
    BlockSource&           region,
    Cell const&            sign,
    Player&                seller,
    ShopConfig const&      config,
    TradeSubmission const& submission
) {
    TradeFacts facts    = basicTradeFacts(TradeDirection::Sell, seller, sign);
    auto       prepared = prepareSell(region, sign, seller, config);
    if (!prepared.context) {
        return withFacts(reject(std::move(prepared.error), prepared.reason), facts);
    }
    auto context = std::move(prepared.context);
    fillTradeFacts(facts, context->shop, config, context->product, context->request.unitPrice, *context->provider);

    TradeRequest request = context->request;
    request.requested    = submission.units;

    auto const decision = planTrade(request);
    if (!decision.ok()) {
        return withFacts(
            reject(tradeRejectionMessage(TradeDirection::Sell, decision.rejection), decision.rejection),
            facts
        );
    }
    TradePlan const plan = *decision.plan;

    if (auto confirm = confirmIfChanged(
            TradeDirection::Sell,
            context->shop.info,
            plan,
            submission
        )) {
        return std::move(*confirm);
    }

    auto&               provider = *context->provider;
    ShopInfoData const& info     = context->shop.info;
    // 卖出：店那一侧付钱、玩家收钱（DESIGN §8.5 的表格）。普通店的付款方是店主，
    // 管理员店是系统账户（§7.4）：钱凭空记到玩家账上，所以不读也不限制「店主余额」。
    // 商店税同样由店那一侧承担：玩家收到的仍是牌面金额 `total`，店主另付一笔 `tax`（§8.9）。
    Payment const     payment = paymentFor(TradeDirection::Sell, info, context->shop.ownerXuid, seller.getXuid(), plan);
    std::string const freeze  = freezeKey(seller.getXuid(), region.getDimensionId(), sign);

    // 主交易金额：卖出方向玩家收到的就是牌面额（税是店主另付的），所以它等于 `总额`。
    facts.gross = payment.main;

    TradeFrame frame{
        region,
        seller,
        context->container,
        TradeSnapshots{context->container, seller.getInventory()},
        provider,
        payment
    };

    // 第 5 步：先动钱——店主付给玩家。理由与买入相同：金额接口失败时两边余额
    // 一分不动，而物品搬运可能部分成功（DESIGN §10.2 第 5 步）。
    //
    // 先给钱后取货不构成复制窗口：钱与货是两种东西，玩家拿到钱不会让货变多；
    // 真正要防的是「同一件货同时在两处」，那由下一步的顺序保证。
    if (auto money = moveMoney(TradeDirection::Sell, provider, payment, freeze)) {
        return withFacts(std::move(*money), facts);
    }

    // 第 6 步：先从背包取货、再放进商店箱。
    //
    // 顺序与买入对称，效果也一样：中间那一刻商品**两处都没有**（背包已经少了、
    // 箱子还没多），这是一段「丢失窗口」而不是「复制窗口」，退路是快照回写 + 退款。
    // 反过来先塞箱子再扣背包，中间那一刻就是同一件货既在箱子里又在背包里——那才是复制。
    Inventory& inventory = seller.getInventory();
    // 管理员店：取出来的货不进箱子，直接销毁（票 02）。销毁之前先把这些堆**复制**一份留着，
    // 因为 `removeItem` 之后箱子与背包里都没有它们了，SNBT 只能从这份副本读。
    std::vector<ItemStack> destroyed;
    if (!takeMatchingItems(
            inventory,
            nullptr,
            inventory.getContainerSize(),
            context->product,
            plan.items,
            info.adminShop ? &destroyed : nullptr,
            /*sampleSlot=*/std::nullopt
        )) {
        // 取货失败（背包在他打开表单之后变少了）：快照把背包写回、退款，一件也不会被销毁。
        return withFacts(
            undoAndRefund(
                frame,
                /*restoreBackpack=*/true,
                tradeRejectionMessage(TradeDirection::Sell, TradeRejection::NotEnoughGoods),
                tradeRejectionReasonName(TradeRejection::NotEnoughGoods),
                freeze
            ),
            facts
        );
    }

    // 箱子装不下时整笔退回：容量的偏差只可能来自「算完之后箱子又变了」，
    // 所以先把背包与箱子都按快照写回，再退款。
    // 管理员店没有这一步：货是销毁的，永远「收得下」，模板箱也一件不动。
    if (!info.adminShop && !putItemsIntoShopBox(context->container, context->product, plan.items)) {
        return withFacts(
            undoAndRefund(
                frame,
                /*restoreBackpack=*/true,
                tradeRejectionMessage(TradeDirection::Sell, TradeRejection::NoRoomToReceive),
                tradeRejectionReasonName(TradeRejection::NoRoomToReceive),
                freeze
            ),
            facts
        );
    }

    // 第 5 步的后半段：店主另付商店税（DESIGN §8.9）。失败时物品与主交易一起退回，
    // 此时税还没被销毁，所以退款金额就是玩家 / 店主真正移动过的 `plan.total`。
    if (auto const taxed = settleTax(frame, freeze)) {
        return withFacts(std::move(*taxed), facts);
    }

    seller.sendInventory(true);
    if (!info.adminShop) {
        refreshShopBox(region, context->container); // 管理员店的箱子一个字都没动
    }

    // 留档在 `Done` 之前造好（SNBT 只有这一刻读得到），落盘在 `Done` 之后（见 `finishTrade`）。
    // 走到这里所有步骤都成功了，所以 `destroyed` 里这些堆确实已经离开玩家的背包。
    std::vector<DiscardRecord> discarded;
    if (info.adminShop) {
        discarded = describeDiscardedItems(nowIso8601Local(), seller.getDimensionId(), sign, seller, destroyed);
        destroyed.clear(); // 销毁：从此没有任何地方还持有这些物品
    }

    Text message = text::keyed("shop.trade.sold", ss::shop::Tone::Default, {
        text::lit(std::to_string(plan.units)),
        text::lit(info.itemName),
        text::lit(std::to_string(plan.items)),
    });
    if (plan.total > 0) {
        message = text::sequence({
            std::move(message),
            text::keyed("shop.trade.income_suffix", ss::shop::Tone::Default, {
                text::lit(moneyText(info.currencySymbol, plan.total)),
            }),
        });
    }
    if (plan.units < submission.units) {
        // 部分成交：如实说清原因（DESIGN §10.3）。管理员店的钱与箱空间都没有上限，只有你手里的货。
        message = text::sequence({
            std::move(message),
            text::keyed(
                info.adminShop ? "shop.trade.partial_sell_admin" : "shop.trade.partial_sell",
                ss::shop::Tone::Default,
                {text::lit(std::to_string(plan.units))}
            ),
        });
    }
    return withFacts(
        TradeResult{
            .status    = TradeStatus::Done,
            .plan      = plan,
            .message   = std::move(message),
            .discarded = std::move(discarded),
        },
        facts
    );
}

void requestBuyForm(Player& viewer, Cell const& sign) {
    // 弹份数表单也要读世界（商店箱、余额、背包），所以同样先切回服务端线程。
    // 这一步不计冷却：点牌已经过了一道，这里只是把第二张表单画出来。
    onServerThread(
        viewer,
        sign,
        /*useCooldown=*/false,
        [](Player& player, Cell const& shopSign, ShopConfig const& config) {
            requestUnitsForm(player, shopSign, TradeDirection::Buy, config, prepareBuy);
        }
    );
}

void requestSellForm(Player& viewer, Cell const& sign) {
    onServerThread(
        viewer,
        sign,
        /*useCooldown=*/false,
        [](Player& player, Cell const& shopSign, ShopConfig const& config) {
            requestUnitsForm(player, shopSign, TradeDirection::Sell, config, prepareSell);
        }
    );
}

void submitBuy(Player& buyer, Cell const& sign, TradeSubmission const& submission) {
    submitTrade(buyer, sign, TradeDirection::Buy, submission, buyUnits);
}

void submitSell(Player& seller, Cell const& sign, TradeSubmission const& submission) {
    submitTrade(seller, sign, TradeDirection::Sell, submission, sellUnits);
}

void initTradeService(ShopConfig const& config) {
    if (gSubmitGate) {
        return;
    }
    gSubmitGate.emplace(config.shopInteractionCooldownMs);
}

void resetTradeService() {
    gSubmitGate.reset();
    // 冻结名单**不在这里清**：`/ll reload Shop` 与关闭进程都走 disable()，
    // 在这里清等于让一次无关重载把「钱货还没对上」的交易对静默解冻。
    // 注意它只能挡住「disable() 主动清」这一条路——加载器若整块卸载 DLL，静态对象仍会消失。
    // 要真跨过那个边界必须落盘（未做）；卸载时的 WARN 是最低限度的提示。
}

std::size_t frozenTradeCount() { return gFrozenTrades.size(); }

} // namespace ss::shop
