#include "mod/ContainerFinderBds.h"

#include "mod/ShopOwner.h"

#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/nbt/CompoundTag.h"
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
#include "shop/DestructionProtection.h"
#include "shop/ShopSign.h"

#include <algorithm>
#include <array>
#include <cstdint>
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

/// 按合并后的逻辑槽位遍历：先附着那一半，再另一半。
///
/// 顺序与边界由 `mergedSlots` 说了算，这里不再自己算一遍。
/// `fn` 返回 false 就提前结束（找到商品模板后不用再扫剩下的格子）。
template <class Fn>
void forEachMergedSlot(Container& primary, Container* secondary, int slots, Fn&& fn) {
    for (auto const& slot : mergedSlots(primary, secondary, slots)) {
        if (!fn(slot.container->getItem(slot.slot))) {
            return;
        }
    }
}

/// 商品模板 = 商店箱里（双箱合并）第一个非空槽位的物品（附着那一半优先，DESIGN §5.3）。
ItemStack const* firstNonEmptyStack(Container& primary, Container* secondary, int slots) {
    ItemStack const* product = nullptr;
    forEachMergedSlot(primary, secondary, slots, [&](ItemStack const& stack) {
        if (stack.isNull()) {
            return true;
        }
        product = &stack;
        return false;
    });
    return product;
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
    auto*        chest     = static_cast<ChestBlockActor*>(actor);
    Container*   secondary = nullptr;
    ContainerRef ref{toCell(pos), chest->getContainerSize(), std::nullopt};
    if (auto* paired = chest->mLargeChestPaired) {
        secondary     = paired;
        ref.pairedPos = toCell(chest->mLargeChestPairedPosition);
        ref.size      += secondary->getContainerSize();
    }
    return ContainerAt{ref, chest, secondary};
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

std::int64_t countMatchingItems(Container& primary, Container* secondary, int slots, ItemStackBase const& product) {
    std::int64_t total = 0;
    forEachMergedSlot(primary, secondary, slots, [&](ItemStack const& stack) {
        if (!stack.isNull() && sameShopItem(stack, product)) {
            total += stack.mCount;
        }
        return true;
    });
    return total;
}

std::vector<MergedSlot> mergedSlots(Container& primary, Container* secondary, int size) {
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

bool sameShopItem(ItemStackBase const& lhs, ItemStackBase const& rhs) {
    if (lhs.getId() != rhs.getId() || lhs.getAuxValue() != rhs.getAuxValue()) {
        return false;
    }
    if (lhs.mUserData == nullptr || rhs.mUserData == nullptr) {
        return lhs.mUserData == rhs.mUserData;
    }
    return lhs.mUserData->equals(*rhs.mUserData);
}

ItemStack const* firstProductStack(AttachedContainer const& attached) {
    if (attached.primary == nullptr) {
        return nullptr;
    }
    return firstNonEmptyStack(*attached.primary, attached.secondary, attached.ref.size);
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
    ItemStack const* product = firstNonEmptyStack(*box.primary, box.secondary, box.ref.size);
    if (product == nullptr) {
        return {.ref = box.ref, .error = ContainerError::Empty};
    }

    return {
        .ref       = box.ref,
        .error     = ContainerError::None,
        .itemName  = product->getName(),
        .itemCount = countMatchingItems(*box.primary, box.secondary, box.ref.size, *product),
    };
}

std::vector<AttachedShopSign>
attachedShopSignsAt(BlockSource& region, BlockPos const& containerPos, ShopConfig const& config) {
    auto const container = containerAt(region, containerPos);
    if (!container) {
        return {};
    }

    auto const                 signConfig = toSignConfig(config);
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

        if (auto owner = shopSignOwnerHere(region, signPos, signConfig)) {
            bound.push_back(AttachedShopSign{geometry, *owner});
        }
    });
    return bound;
}

std::optional<ShopOwner> shopSignOwnerAt(BlockSource& region, BlockPos const& signPos, ShopConfig const& config) {
    return shopSignOwnerHere(region, signPos, toSignConfig(config));
}

std::vector<AttachedShopSign>
shopSignsWithAttachedCell(BlockSource& region, BlockPos const& attachedCell, ShopConfig const& config) {
    auto const signConfig = toSignConfig(config);

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
        if (auto owner = shopSignOwnerHere(region, toBlockPos(sign.pos), signConfig)) {
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

} // namespace ss::shop
