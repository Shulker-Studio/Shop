#include "core/container/ContainerFinderBds.h"

#include "core/container/SampleSlot.h"

#include "util/log/DebugLog.h"
#include "Shop.h"
#include "core/shop/ShopOwner.h"

#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/deps/nbt/ListTag.h"
#include "mc/world/Container.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/actor/BlockActorType.h"
#include "mc/world/level/block/actor/ChestBlockActor.h"
#include "mc/world/level/block/actor/SignBlockActor.h"
#include "mc/world/level/block/states/BlockState.h"
#include "mc/world/level/block/states/BlockStateInstance.h"
#include "core/container/DestructionProtection.h"
#include "core/shop/ProductRules.h"
#include "core/sign/ShopSign.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

namespace ss::shop {

SignBlockActor* signBlockActorAt(BlockSource& region, BlockPos const& pos) {
    auto* actor = region.getBlockEntity(pos);
    if (actor == nullptr) {
        return nullptr;
    }
    if (!actor->isTypeOrDerived(BlockActorType::Sign) && !actor->isTypeOrDerived(BlockActorType::HangingSign)) {
        return nullptr;
    }
    return static_cast<SignBlockActor*>(actor);
}

BlockPos toBlockPos(Cell const& cell) { return BlockPos{cell.x, cell.y, cell.z}; }

Cell toCell(BlockPos const& pos) { return Cell{pos.x, pos.y, pos.z}; }

namespace {

bool endsWith(std::string_view text, std::string_view suffix) {
    return text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix;
}

/// 方块状态的运行时名字带命名空间（`minecraft:facing_direction`），比较时两边都认。
bool stateNameIs(std::string_view fullName, std::string_view wanted) {
    constexpr std::string_view kNamespace = "minecraft:";
    if (fullName == wanted) {
        return true;
    }
    return fullName.size() == kNamespace.size() + wanted.size() && fullName.starts_with(kNamespace)
        && fullName.substr(kNamespace.size()) == wanted;
}

/// 按名字在方块的方块状态表里找状态 ID。找不到返回空。
std::optional<uint64> stateIdByName(BlockType const& type, std::string_view name) {
    for (auto const& [id, instance] : type.mStates.get()) {
        HashedString const& stateName = instance.mState->mName;
        if (stateNameIs(stateName.getString(), name)) {
            return id;
        }
    }
    return std::nullopt;
}

std::optional<int> readIntState(Block const& block, std::string_view name) {
    auto const id = stateIdByName(block.getBlockType(), name);
    if (!id) {
        return std::nullopt;
    }
    return block.getState<int>(*id);
}

std::optional<bool> readBoolState(Block const& block, std::string_view name) {
    auto const id = stateIdByName(block.getBlockType(), name);
    if (!id) {
        return std::nullopt;
    }
    return block.getState<bool>(*id);
}

/// `facing_direction`（2 北 / 3 南 / 4 西 / 5 东）或 `cardinal_direction`
/// （`Direction::Type`：0 南 / 1 西 / 2 北 / 3 东）→ 水平朝向。
///
/// 朝向是牌子正面**朝外**的方向，附着点在它的反方向（PowerNukkit 的
/// `BlockWallSign::onUpdate` 用 `getBlockFace().getOpposite()` 找支撑方块）。
std::optional<HorizontalFacing> readHorizontalFacing(Block const& block) {
    if (auto facing = readIntState(block, "facing_direction")) {
        switch (*facing) {
        case 2:
            return HorizontalFacing::North;
        case 3:
            return HorizontalFacing::South;
        case 4:
            return HorizontalFacing::West;
        case 5:
            return HorizontalFacing::East;
        default:
            return std::nullopt;
        }
    }
    if (auto cardinal = readIntState(block, "cardinal_direction")) {
        switch (*cardinal) {
        case 0:
            return HorizontalFacing::South;
        case 1:
            return HorizontalFacing::West;
        case 2:
            return HorizontalFacing::North;
        case 3:
            return HorizontalFacing::East;
        default:
            return std::nullopt;
        }
    }
    return std::nullopt;
}

bool isAllowedType(std::string const& typeName, std::vector<std::string> const& allowed) {
    for (auto const& candidate : allowed) {
        if (candidate == typeName) {
            return true;
        }
    }
    return false;
}

/// 商品模板 = 商店箱里（双箱合并）第一个非空槽位的物品（附着那一半优先，DESIGN §5.3）。
///
/// 管理员店管这一格叫**样品槽**（票 03）。「第一个非空」这条规则本身在纯逻辑层
/// （`shop/SampleSlot.h` 的 `sampleSlotIndex`），这里只负责回答「第 i 格空不空」——
/// 于是「0 号空、1 号有钻石」这种情形能在单测里直接断言，不必造容器。
///
/// 返回的是**合并顺序里的下标**而不是裸指针：样品槽永不出售，计数与扣货都要知道「第几格」，
/// 而「第几格」只有这里算得出来。
std::optional<int> firstNonEmptySlot(std::vector<MergedSlot> const& merged) {
    return sampleSlotIndex(static_cast<int>(merged.size()), [&merged](int slot) {
        MergedSlot const& at = merged[static_cast<std::size_t>(slot)];
        return at.container->getItem(at.slot).isNull();
    });
}

/// `Items` 列表里的一个格子转成 compound；结构不对返回空（不猜、不修数据）。
CompoundTag const* cellCompound(Tag const& cell) {
    return cell.getId() == Tag::Compound ? cell.as_ptr<CompoundTag>() : nullptr;
}

/// 用户数据里的 `Items` 列表：潜影盒的内容在这里（收纳袋不在这里——它的内容在服务端的
/// 动态容器注册表里，只剩一个引用，所以它整类不许当商品，见 `shop/ProductRules.h`）。
///
/// 只认 `Items` 这个名字（顶层，或 `BlockEntityTag` 里的一层），**不猜别的名字**：猜错会把
/// 附魔书、旗帜图案这类“本来就有 tag 的物品”当成盒子。读不出来时返回空，于是上层退到
/// 「整段用户数据相等」的旧口径——猜错只会让混装检查失效，不会让不同内容的盒子被当成同一种。
ListTag const* contentsIn(CompoundTag const& tag) {
    auto const* direct = tag.get("Items");
    if (direct != nullptr && direct->getId() == Tag::List) {
        return direct->as_ptr<ListTag>();
    }
    auto const* wrapper = tag.get("BlockEntityTag");
    if (wrapper == nullptr || wrapper->getId() != Tag::Compound) {
        return nullptr;
    }
    auto const* nested = wrapper->as<CompoundTag>().get("Items");
    if (nested != nullptr && nested->getId() == Tag::List) {
        return nested->as_ptr<ListTag>();
    }
    return nullptr;
}

std::unique_ptr<CompoundTag> userDataOf(ItemStackBase const& item) {
    return item.mUserData == nullptr ? nullptr : item.mUserData->clone();
}

/// 把容器两半合成一个 `ContainerRef`，并带上要读写物品的两个指针。不看白名单。
struct ContainerAt {
    ContainerRef ref;
    Container*   primary   = nullptr;
    Container*   secondary = nullptr;
};

/// 这一格的容器本体（双箱已合并）。不是容器方块返回空。
std::optional<ContainerAt> containerAt(BlockSource& region, BlockPos const& pos) {
    auto* actor = region.getBlockEntity(pos);
    if (actor == nullptr
        || (!actor->isTypeOrDerived(BlockActorType::Chest) && !actor->isTypeOrDerived(BlockActorType::BarrelBlock))) {
        return std::nullopt;
    }

    // `BarrelBlockActor` 本身派生自 `ChestBlockActor`（mc/world/level/block/actor/BarrelBlockActor.h），
    // 所以这个向下转型对木桶也合法；木桶永远不会配对，双箱分支自然走不到。
    auto*      chest     = static_cast<ChestBlockActor*>(actor);
    Container* primary   = chest;
    Container* secondary = nullptr;

    // 配对之后 BDS **可能**已经把自己当成一个 54 格的逻辑容器（`getContainerSize()` 报 54、
    // `getItem` 跨两半），也可能只报自己那一半的 27 格——头文件里看不出来：
    // `ChestBlockActor::getContainerSize` 是有实现的真函数（非 `MCFOLD`），既可能返回成员，
    // 也可能按配对算。所以这里不猜，按**物理事实**判：单半个箱子的格子数就是 27。
    //
    // 超过 27 = 引擎自己已经合并了，那就**不许**再把另一半加一遍：
    // 加了会把每一格数两遍，双箱的可成交份数与“还能装几份”全部翻倍（单箱看不出问题）。
    constexpr int kSingleChestSlots = 27;
    int const     attachedSlots     = chest->getContainerSize();
    ContainerRef  ref{toCell(pos), attachedSlots, std::nullopt};
    if (auto* paired = chest->mLargeChestPaired) {
        ref.pairedPos = toCell(chest->mLargeChestPairedPosition);
        int const pairedSlots = paired->getContainerSize();
        // 两半都只报自己那一半的 27 格 = 引擎没合并，才由这里手工拼成 54 格。
        if (attachedSlots <= kSingleChestSlots && pairedSlots <= kSingleChestSlots) {
            secondary = paired;
            ref.size  = attachedSlots + pairedSlots;
        } else {
            // 任一半报出比半个箱子更多的格子，说明这一对在引擎眼里已经是一个逻辑容器：
            // 读写全交给**报得更多**的那一半（合并后的视图就在它身上），另一半一次都不走。
            // 两半报的数不一致时这样才不会只读到较窄的那一半；`ref.pos` / `ref.pairedPos`
            // 仍按物理位置（附着那一半 + 另一半）记，刷新与保护认的是物理那一对。
            if (pairedSlots > attachedSlots) {
                primary = paired;
            }
            ref.size = std::max(attachedSlots, pairedSlots);
        }
    }
    return ContainerAt{ref, primary, secondary};
}

/// 这一格上的牌是不是「合法商店牌」。只有合法商店牌才算占用了容器。
bool isShopSign(BlockSource& region, BlockPos const& pos, ShopSignConfig const& signConfig) {
    auto* sign = signBlockActorAt(region, pos);
    if (sign == nullptr) {
        return false;
    }
    return isShopSignFront(*sign, signConfig);
}

/// 这一格上的告示牌如果「像商店牌」，返回它的店主（DESIGN §9.2 的保护判据）。
///
/// 与 `isShopSign`（现在严格合法）故意不同：只看正面文本**像不像**商店牌，
/// 不看附着容器。改配置、脏数据、箱子被拆掉，都不该让商店牌失去保护。
std::optional<ShopOwner> shopSignOwnerHere(BlockSource& region, BlockPos const& pos, ShopSignConfig const& signConfig) {
    auto* sign = signBlockActorAt(region, pos);
    if (sign == nullptr) {
        return std::nullopt;
    }
    auto const parsed = parseShopSignFront(*sign, signConfig);
    if (!parsed.looksLikeShopSign()) {
        return std::nullopt;
    }
    return readShopOwner(*sign, parsed);
}

/// 容器（含双箱另一半）六个面周围的每一块告示牌：回调拿到位置与几何。
/// 「扫哪几格」只在这里定义一次——开店时的冲突检查与保护时的反查共用。
template <class Fn>
void forEachSignAround(BlockSource& region, ContainerRef const& container, Fn&& fn) {
    auto const around = [&](Cell const& containerCell) {
        for (Cell const& cell : neighboursOf(containerCell)) {
            BlockPos const pos = toBlockPos(cell);
            if (auto const geometry = readSignGeometry(region, pos)) {
                fn(pos, *geometry);
            }
        }
    };

    around(container.pos);
    if (container.pairedPos) {
        around(*container.pairedPos);
    }
}

/// 收集容器（含双箱另一半）六个面周围的牌子，交给纯逻辑判定冲突。
std::vector<NeighbourSign>
collectNeighbourSigns(BlockSource& region, ContainerRef const& container, ShopSignConfig const& signConfig) {
    std::vector<NeighbourSign> signs;
    forEachSignAround(region, container, [&](BlockPos const& pos, SignRef const& geometry) {
        signs.push_back({geometry, isShopSign(region, pos, signConfig)});
    });
    return signs;
}

} // namespace

std::int64_t countMatchingItems(
    Container&           primary,
    Container*           secondary,
    int                  slots,
    ItemStackBase const& product,
    std::optional<int>   sampleSlot
) {
    auto const   merged = mergedSlots(primary, secondary, slots);
    std::int64_t total  = 0;
    // 槽位顺序由 `saleableSlotOrder` 说了算：传了样品槽就只数可卖的格（倒序、跳过样品槽）；
    // 没传（玩家背包、总数统计）就只是倒序遍历一遍。一个函数、一个谓词、一个顺序。
    for (int const index : saleableSlotOrder(static_cast<int>(merged.size()), sampleSlot)) {
        MergedSlot const& at    = merged[static_cast<std::size_t>(index)];
        ItemStack const&  stack = at.container->getItem(at.slot);
        if (!stack.isNull() && sameShopItem(stack, product)) {
            total += stack.mCount;
        }
    }
    return total;
}

std::vector<MergedSlot> mergedSlots(Container& primary, Container* secondary, int size) {
    // `size` 是**逻辑总格数**，而且必须得是一次数出来的（`containerAt` 里那一条）：
    // 先走 primary 直到 primary 自己的格数或 `size` 用完，再把剩下的额度给 secondary。
    // 所以只要 `size` 没多算，两半就各自只被走一遍；`secondary` 为空（单箱/木桶/引擎已自己合并）时
    // 就全给 primary 一个人。
    std::vector<MergedSlot> slots;
    if (size <= 0) {
        return slots;
    }
    slots.reserve(static_cast<std::size_t>(size));

    int remaining = size;
    for (int slot = 0; slot < primary.getContainerSize() && remaining > 0; ++slot, --remaining) {
        slots.push_back({&primary, slot});
    }
    if (secondary == nullptr) {
        return slots;
    }
    int const limit = std::min(secondary->getContainerSize(), remaining);
    for (int slot = 0; slot < limit; ++slot) {
        slots.push_back({secondary, slot});
    }
    return slots;
}

/// 两侧用户数据里**与容器内容无关**的那部分是否相同。
///
/// “无关”指的是 `Items`（顶层与 `BlockEntityTag` 里那一层）：内容已经单独按多重集比过了。
/// 规范化后“没有用户数据”与“只有一条空 `Items`”算同一件事——原实现把这两种判成不同种，
/// 玩家会看到两个“一模一样”的盒子却不能当同一种商品。
bool sameUserDataOutsideContents(ItemStackBase const& lhs, ItemStackBase const& rhs) {
    auto normalized = [](ItemStackBase const& item) {
        auto copy = userDataOf(item);
        if (!copy) {
            return copy;
        }
        copy->erase("Items");
        auto const* wrapperTag = copy->get("BlockEntityTag");
        // 只有真的是 compound 才能往下删：脏数据 / 别人写的别的形状都不能让这里炸掉。
        if (wrapperTag != nullptr && wrapperTag->getId() == Tag::Compound) {
            auto& wrapper = copy->at("BlockEntityTag").get<CompoundTag>();
            wrapper.erase("Items");
            if (wrapper.empty()) {
                // 核空的外壳不算“有用户数据”。
                copy->erase("BlockEntityTag");
            }
        }
        return copy;
    };

    auto left  = normalized(lhs);
    auto right = normalized(rhs);
    bool const leftEmpty  = !left || left->empty();
    bool const rightEmpty = !right || right->empty();
    if (leftEmpty || rightEmpty) {
        return leftEmpty == rightEmpty;
    }
    return left->equals(*right);
}

/// 一格内容是否相同：去掉 `Slot` 后整段相等。
///
/// 格位不是身份——同一格物品放在第 1 格还是第 5 格不是两种商品（空格都不算，格位更不算）。
bool sameCellTag(CompoundTag const& lhs, CompoundTag const& rhs) {
    auto left = lhs.clone();
    auto right = rhs.clone();
    left->erase("Slot");
    right->erase("Slot");
    return left->equals(*right);
}

/// 两侧内容是不是同一个多重集：不考虑顺序，也不考虑格位。
bool contentsMatch(ListTag const& lhs, ListTag const& rhs) {
    if (lhs.size() != rhs.size()) {
        return false;
    }
    std::vector<bool> used(rhs.size(), false);
    for (auto const& cell : lhs) {
        auto const* cellTag = cellCompound(*cell);
        if (cellTag == nullptr) {
            return false;
        }
        bool matched = false;
        for (std::size_t index = 0; index < rhs.size(); ++index) {
            if (used[index]) {
                continue;
            }
            auto const* other = cellCompound(*rhs[index]);
            if (other == nullptr || !sameCellTag(*cellTag, *other)) {
                continue;
            }
            used[index] = true;
            matched     = true;
            break;
        }
        if (!matched) {
            return false;
        }
    }
    return true;
}

bool sameShopItem(ItemStackBase const& lhs, ItemStackBase const& rhs) {
    if (lhs.getId() != rhs.getId() || lhs.getAuxValue() != rhs.getAuxValue()) {
        return false;
    }
    // 容器类物品：盒内内容也是身份的一部分（DESIGN §5.3）。读不出内容时两侧都是空，
    // 于是退到下面那句“去掉 Items 后整段相等”——旧口径，安全但不区分格位。
    auto const* leftContents  = lhs.mUserData == nullptr ? nullptr : contentsIn(*lhs.mUserData);
    auto const* rightContents = rhs.mUserData == nullptr ? nullptr : contentsIn(*rhs.mUserData);
    if ((leftContents == nullptr) != (rightContents == nullptr)) {
        return false;
    }
    if (leftContents != nullptr && !contentsMatch(*leftContents, *rightContents)) {
        return false;
    }
    return sameUserDataOutsideContents(lhs, rhs);
}

bool containerProductMixed(ItemStackBase const& product) {
    if (product.mUserData == nullptr) {
        return false;
    }
    auto const* contents = contentsIn(*product.mUserData);
    // 不是容器类物品，或者盒子是空的：没有“整不整齐”可谈。
    // （`Items` 里只会有非空格，所以空格不参与判定是天然的。）
    if (contents == nullptr || contents->empty()) {
        return false;
    }
    auto const* first = cellCompound(*(*contents)[0]);
    if (first == nullptr) {
        // 读不出格子结构（脏数据）时不拦：让后端的同种比较去回答“是不是同一种”，
        // 不能让一个读不懂的 tag 把整个商品报成不可用。
        return false;
    }
    for (std::size_t index = 1; index < contents->size(); ++index) {
        auto const* cell = cellCompound(*(*contents)[index]);
        if (cell == nullptr || !sameCellTag(*first, *cell)) {
            return true;
        }
    }
    return false;
}

namespace {

/// 上一次打出去的 `product_nbt` 结论，用来掐掉重复行（只在结论变化时打）。
std::string gLastProductNbt;

/// 顶层键名清单，用 `,` 连起来（值里的空格由 `dbgField` 换掉）。
std::string tagKeyList(CompoundTag const& tag) {
    std::string out;
    for (auto const& entry : tag.mTags) {
        if (!out.empty()) {
            out.push_back(',');
        }
        out += entry.first;
    }
    return out;
}

/// 真机探针：把商品物品的用户数据形状打成一行 `[shop/dbg] product_nbt …`。
///
/// 存在的唯一理由：头文件答不出潜影盒/收纳袋的 `Items` 到底挂在哪一层
/// （顶层还是 `BlockEntityTag` 里），而这两种写法线上都见过。`items_at` 直接给出结论，
/// 读不到时 `snbt` 里就是原始材料——照着它改 `contentsIn` 的键，而不是靠猜。
/// 由 `/shop debug on` 控制，默认一行都不打。
void logProductNbt(ItemStackBase const& product) {
    auto const  tag   = userDataOf(product);
    std::string where = "none";
    int         cells = 0;
    std::string keys;
    std::string blockKeys;
    std::string snbt;
    if (tag != nullptr) {
        keys = tagKeyList(*tag);
        if (auto const* list = contentsIn(*tag); list != nullptr) {
            where = tag->contains("Items") ? "top" : "block_entity_tag";
            cells = static_cast<int>(list->size());
        } else {
            // 没找到 `Items` 才是最需要原始材料的情况：一行 SNBT，截断免得刷屏。
            snbt = tag->toString();
            if (snbt.size() > 240) {
                snbt.resize(240);
                snbt += "...";
            }
        }
        auto const* wrapper = tag->get("BlockEntityTag");
        if (wrapper != nullptr && wrapper->getId() == Tag::Compound) {
            blockKeys = tagKeyList(wrapper->as<CompoundTag>());
        }
    }
    std::string line = dbgField("id", product.getTypeName());
    line += ' ';
    line += dbgField("user_data", tag != nullptr);
    line += ' ';
    line += dbgField("items_at", where);
    line += ' ';
    line += dbgField("cells", cells);
    line += ' ';
    line += dbgField("keys", keys);
    line += ' ';
    line += dbgField("be_keys", blockKeys);
    line += ' ';
    line += dbgField("mixed", containerProductMixed(product));
    line += ' ';
    line += dbgField("bundle", productHoldsBundle(product));
    line += ' ';
    line += dbgField("snbt", snbt);

    // 只在**结论变了**的时候打一行：一次点牌会走好几遍判定，同一件商品重复打十几次
    // 只会把日志刷满（真机上已经见过 20 行一样的）。换了商品、换了盒内内容、
    // 或者整不整齐的结论变了，签名就会变，照打。
    if (line == gLastProductNbt) {
        return;
    }
    gLastProductNbt = line;
    shopDebugEvent("product_nbt", {line});
}

/// 一件物品本身、或它盒子里（递归到底）有没有禁售的东西。
bool itemBanned(ItemStackBase const& item, ItemBanList const& bannedItems, int depth) {
    // `getTypeName()` 是带命名空间的物品类型名（`minecraft:bedrock`），不是展示名；
    // `getId()` 是数字 id，禁售表不认它。
    std::string const id = item.getTypeName();
    if (!id.empty() && bannedItems.banned(id)) {
        return true;
    }
    if (depth <= 0 || item.mUserData == nullptr) {
        return false;
    }
    auto const* contents = contentsIn(*item.mUserData);
    if (contents == nullptr) {
        return false;
    }
    for (auto const& cell : *contents) {
        auto const* cellTag = cellCompound(*cell);
        if (cellTag == nullptr) {
            continue;
        }
        // 交给引擎把一格 NBT 解成真正的 ItemStack：`Name`、`tag`、`Damage` 到底怎么摆
        // 是 BDS 的事，自己手读一遍只是再造一个不准的副本。
        ItemStack const inner = ItemStack::fromTag(*cellTag);
        if (!inner.isNull() && itemBanned(inner, bannedItems, depth - 1)) {
            return true;
        }
    }
    return false;
}

/// 一件物品本身、或它盒子里（递归到底）有没有收纳袋。
///
/// 收纳袋自己不能当商品（内容在服务端的动态容器注册表里，读不出来），**装在潜影盒里同样不行**：
/// 那一格的 tag 里只有一个 `bundle_id` 引用，袋里的东西换了 tag 一个字都不会变，
/// 于是 `sameShopItem` 照样判"同一种"——等于留了一条把东西偷换进出商店的缝。
bool itemHoldsBundle(ItemStackBase const& item, int depth) {
    if (isBundleItemId(item.getTypeName())) {
        return true;
    }
    if (depth <= 0 || item.mUserData == nullptr) {
        return false;
    }
    auto const* contents = contentsIn(*item.mUserData);
    if (contents == nullptr) {
        return false;
    }
    for (auto const& cell : *contents) {
        auto const* cellTag = cellCompound(*cell);
        if (cellTag == nullptr) {
            continue;
        }
        ItemStack const inner = ItemStack::fromTag(*cellTag);
        if (!inner.isNull() && itemHoldsBundle(inner, depth - 1)) {
            return true;
        }
    }
    return false;
}

} // namespace

bool productHoldsBundle(ItemStackBase const& product) {
    // 深度上限与禁售共用一套理由：脏数据不许把递归拖进无底洞。
    constexpr int kMaxNestedDepth = 8;
    return itemHoldsBundle(product, kMaxNestedDepth);
}

std::vector<BoxCell> productBoxContents(ItemStackBase const& product) {
    std::vector<BoxCell> cells;
    if (product.mUserData == nullptr) {
        return cells;
    }
    auto const* contents = contentsIn(*product.mUserData);
    if (contents == nullptr) {
        return cells;
    }
    // 一格一格解成真 ItemStack（`Name` / `Count` / `tag` 怎么摆是 BDS 的事），再按格内内容合并：
    // 整齐的盒子因此只剩一项、件数是格子数的和，混装的盒子每项各一行——正是店主需要看到的区别。
    // 格子最多 27 个，O(n²) 的比对比维护一张哈希表划算，也不必给格内 compound 造键。
    std::vector<CompoundTag const*> signatures; // 与 `cells` 一一对应的“这一格是什么”
    for (auto const& cell : *contents) {
        auto const* cellTag = cellCompound(*cell);
        if (cellTag == nullptr) {
            continue;
        }
        ItemStack const inner = ItemStack::fromTag(*cellTag);
        if (inner.isNull() || inner.mCount <= 0) {
            continue;
        }
        auto const it = std::find_if(signatures.begin(), signatures.end(), [&](CompoundTag const* known) {
            return sameCellTag(*known, *cellTag);
        });
        if (it != signatures.end()) {
            cells[static_cast<std::size_t>(it - signatures.begin())].count += inner.mCount;
            continue;
        }
        signatures.push_back(cellTag);
        cells.push_back(BoxCell{inner.getName(), inner.mCount});
    }
    return cells;
}

bool productBanned(ItemStackBase const& product, ItemBanList const& bannedItems) {
    if (bannedItems.empty()) {
        return false;
    }
    // 深度上限：收纳袋理论上能装收纳袋，读到脏数据也不能无限递归。
    constexpr int kMaxNestedDepth = 8;
    return itemBanned(product, bannedItems, kMaxNestedDepth);
}

std::optional<ProductSlot> productSlotOf(AttachedContainer const& attached) {
    if (attached.primary == nullptr) {
        return std::nullopt;
    }
    auto const merged = mergedSlots(*attached.primary, attached.secondary, attached.ref.size);
    auto const index  = firstNonEmptySlot(merged);
    if (!index) {
        return std::nullopt;
    }
    MergedSlot const& at = merged[static_cast<std::size_t>(*index)];
    return ProductSlot{*index, &at.container->getItem(at.slot)};
}

AttachedContainerResult attachedContainerOf(
    BlockSource&                    region,
    BlockPos const&                 signPos,
    std::vector<std::string> const& allowedBlockTypes
) {
    auto const geometry = readSignGeometry(region, signPos);
    if (!geometry) {
        return {.error = ContainerError::UnknownSignShape};
    }

    Cell const     attached    = attachedCellOfSign(geometry->pos, geometry->form, geometry->facing);
    BlockPos const attachedPos = toBlockPos(attached);

    auto container = containerAt(region, attachedPos);
    if (!container) {
        return {.error = ContainerError::NotAContainer};
    }
    if (!isAllowedType(region.getBlock(attachedPos).getTypeName(), allowedBlockTypes)) {
        return {.error = ContainerError::NotAllowedContainer};
    }

    return {.container = AttachedContainer{*geometry, container->ref, container->primary, container->secondary}};
}

std::optional<SignRef> readSignGeometry(BlockSource& region, BlockPos const& signPos) {
    if (signBlockActorAt(region, signPos) == nullptr) {
        return std::nullopt;
    }

    Block const&      block = region.getBlock(signPos);
    std::string const name  = block.getTypeName();

    // 先判悬挂牌：`..._hanging_sign` 也以 `sign` 结尾，但不会以 `wall_sign` 结尾。
    if (endsWith(name, "hanging_sign")) {
        auto const hanging = readBoolState(block, "hanging");
        // 只认「吊在方块底面」：侧挂形态在基岩版里判不出挂在哪一面墙
        // （`facing_direction` 与墙面垂直），猜错会绑到旁边的另一个容器上。
        // 状态读不到也不猜，直接当作形态未知。
        if (!hanging || !*hanging) {
            return std::nullopt;
        }
        return SignRef{toCell(signPos), SignForm::HangingFromCeiling, HorizontalFacing::North};
    }

    if (endsWith(name, "wall_sign")) {
        auto const facing = readHorizontalFacing(block);
        if (!facing) {
            return std::nullopt;
        }
        return SignRef{toCell(signPos), SignForm::Wall, *facing};
    }

    if (endsWith(name, "standing_sign")) {
        return SignRef{toCell(signPos), SignForm::Standing, HorizontalFacing::North};
    }

    return std::nullopt;
}

ContainerLookup
inspectAttachedContainer(BlockSource& region, BlockPos const& signPos, ContainerLookupOptions const& options) {
    auto const attached = attachedContainerOf(region, signPos, options.allowedBlockTypes);
    if (!attached.ok()) {
        return {.error = attached.error};
    }

    AttachedContainer const& box = *attached.container;
    auto const               signs = collectNeighbourSigns(region, box.ref, options.signConfig);
    if (isBoundByAnotherShopSign(signs, box.ref, box.sign.pos)) {
        return {.ref = box.ref, .error = ContainerError::AlreadyBound};
    }

    // 商品 = 商店箱里（双箱合并）第一个非空槽位的物品（附着那一半优先，DESIGN §5.3）。
    auto const product = productSlotOf(box);
    if (!product) {
        return {.ref = box.ref, .error = ContainerError::Empty};
    }

    // 真机探针（见 `logProductNbt`）：`/shop debug on` 之后点一次牌就有一行，讲清
    // 这件商品的 `Items` 挂在哪一层。关掉调试就不产生任何字符串。
    if (shopDebugEnabled()) {
        logProductNbt(*product->stack);
    }

    // 商品已经解析出来了：后面几条判断只决定它**能不能卖**，所以先把商品本身的
    // 信息填进结果，再逐条加错误位。表单在不整齐/被禁售时也要看得见盒里是什么，
    // 那时把这些字段丢掉等于让玩家对着“不可交易”四个字猜。
    ContainerLookup result;
    result.ref         = box.ref;
    result.hasProduct  = true;
    result.itemName    = product->stack->getName();
    result.boxContents = productBoxContents(*product->stack);

    // 收纳袋永远不能当商品，**装在潜影盒里也一样**：内容在服务端的动态容器注册表里，
    // 读不出来（`ProductRules.h`），于是没法核对里面有什么、也没法把内容算进身份。
    // 不分店的性质，所以排在禁售（只管普通店）**前面**——它是能力限制，不是政策。
    if (productHoldsBundle(*product->stack)) {
        result.error = ContainerError::BundleProduct;
        return result;
    }

    // 禁售物品：商品本身、或盒子里嵌套的任意一件被服主禁售，都不能当商品。
    // 排在「混装」前面：这一条是确定的硬禁，混装只是“整理一下就能卖”。
    // `bannedItems == nullptr` = 不判禁售（管理员店，见 `ContainerLookupOptions`）。
    if (options.bannedItems != nullptr && productBanned(*product->stack, *options.bannedItems)) {
        result.error = ContainerError::BannedItem;
        return result;
    }

    // 容器类物品（潜影盒 / 收纳袋）**内部整齐**才能当商品：混装或每格数量不一，
    // 店主就没法用一句话说清“这一份到底是什么”（内容也算身份，见 `sameShopItem`）。
    if (containerProductMixed(*product->stack)) {
        result.error = ContainerError::MixedContainerContents;
        return result;
    }

    // 只算可卖的：样品槽那一份是招牌，不算货。总数（含样品槽）已经没有调用方了,
    // 不存一份“另一个数”在结构里——两份相差不多时早晚会有人拿错。
    result.saleableItemCount =
        countMatchingItems(*box.primary, box.secondary, box.ref.size, *product->stack, product->index);
    return result;
}

std::vector<AttachedShopSign>
attachedShopSignsAt(BlockSource& region, BlockPos const& containerPos, ShopConfig const& config) {
    auto const container = containerAt(region, containerPos);
    if (!container) {
        return {};
    }

    // 牌面配置用 `Shop` 里缓存的那一份：这个函数在漏斗/投掷器的路径上每 tick 都会走到，
    // 而 `toSignConfig` 每次都要拷一个 `std::string`。缓存没有时（模组没加载）退回现算，
    // 于是这个函数在单测/半启动状态下仍然可用。
    ShopSignConfig fallback;
    auto const*    signConfig = currentSignConfig();
    if (signConfig == nullptr) {
        fallback   = toSignConfig(config);
        signConfig = &fallback;
    }
    std::vector<AttachedShopSign> bound;

    forEachSignAround(region, container->ref, [&](BlockPos const& signPos, SignRef const& geometry) {
        // `bindsTo` 已经说明「这块牌附着的正是这个容器」；双箱两半算同一个库存。
        if (!bindsTo(geometry, container->ref)) {
            return;
        }
        // 同一块牌可能同时是两半的邻居（双箱接缝上方），只收一次。
        bool const seen = std::any_of(bound.begin(), bound.end(), [&](AttachedShopSign const& candidate) {
            return candidate.sign.pos == geometry.pos;
        });
        if (seen) {
            return;
        }

        if (auto owner = shopSignOwnerHere(region, signPos, *signConfig)) {
            bound.push_back(AttachedShopSign{geometry, *owner});
        }
    });
    return bound;
}

std::optional<ShopOwner> shopSignOwnerAt(BlockSource& region, BlockPos const& signPos, ShopConfig const& config) {
    // 同上：走缓存。爆炸的方块列表动辄上百格，每一格都会问到这里。
    ShopSignConfig fallback;
    auto const*    signConfig = currentSignConfig();
    if (signConfig == nullptr) {
        fallback   = toSignConfig(config);
        signConfig = &fallback;
    }
    return shopSignOwnerHere(region, signPos, *signConfig);
}

std::vector<AttachedShopSign>
shopSignsWithAttachedCell(BlockSource& region, BlockPos const& attachedCell, ShopConfig const& config) {
    ShopSignConfig fallback;
    auto const*    signConfig = currentSignConfig();
    if (signConfig == nullptr) {
        fallback   = toSignConfig(config);
        signConfig = &fallback;
    }

    // 先拿到六个邻居里能读出几何的（残牌、侧挂的悬挂牌读不出来，直接跳过）
    std::vector<SignRef> geometries;
    for (Cell const& cell : neighboursOf(toCell(attachedCell))) {
        if (auto const geometry = readSignGeometry(region, toBlockPos(cell))) {
            geometries.push_back(*geometry);
        }
    }

    // 「谁的附着格是这一格」的几何口径在纯逻辑层，这里不另算一份偏移
    std::vector<AttachedShopSign> attached;
    for (SignRef const& sign : signsAttachedTo(toCell(attachedCell), geometries)) {
        if (auto owner = shopSignOwnerHere(region, toBlockPos(sign.pos), *signConfig)) {
            attached.push_back(AttachedShopSign{sign, *owner});
        }
    }
    return attached;
}

std::optional<int> facingDirectionAt(BlockSource& region, BlockPos const& pos) {
    // 状态名的命名空间差异（`minecraft:facing_direction`）由 `readIntState` 统一处理，
    // 这里不另写一份「按名字找状态 ID」的循环。
    return readIntState(region.getBlock(pos), "facing_direction");
}

bool isShopBlockAt(BlockSource& region, BlockPos const& pos, ShopConfig const& config) {
    // 便宜的粗筛：商店牌与容器都必有方块实体，没方块实体的格子一定不是。
    // 爆炸列表动辄上百格，大部分被这一句挡掉，不必每格都解析牌面文本。
    if (region.getBlockEntity(pos) == nullptr) {
        return false;
    }
    return shopSignOwnerAt(region, pos, config).has_value() || !attachedShopSignsAt(region, pos, config).empty();
}

bool isShopContainerBlockAt(BlockSource& region, BlockPos const& pos, ShopConfig const& config) {
    // 两层粗筛，按「便宜的先」排。漏斗每 tick 都跑 
    // `ContentProtectionHooks` 那四条 hook，这条路径上省下的每一次 `getBlockEntity`
    // 都要乘上「漏斗数 × 20」。
    //
    // 1. 没方块实体：一定不是（空气、泥土、作物、世界边界……）。
    // 2. 不是箱子/木桶：一定不是（漏斗、漏斗矿车、熔炉、投掷器自身）。
    //    **这一句是最重要的一句**：漏斗链（漏斗上方是另一个漏斗）非常常见，
    //    只靠第 1 层的话它会一路走到「扫 6 个邻居 + 2 次 `toSignConfig` 的字符串拷贝」。
    // 3. 才付 `attachedShopSignsAt` 的代价（六邻居扫描 + 牌面解析）。
    if (region.getBlockEntity(pos) == nullptr) {
        return false;
    }
    if (!containerAt(region, pos)) {
        return false;
    }
    return !attachedShopSignsAt(region, pos, config).empty();
}

} // namespace ss::shop
