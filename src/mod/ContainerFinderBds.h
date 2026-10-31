#pragma once

#include "mod/Config.h"
#include "shop/ContainerFinder.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

class BlockPos;
class BlockSource;

namespace ss::shop {

/// 查看附着容器的结果。`None` 表示可以开店。
enum class ContainerError {
    None,
    /// 这一格不是告示牌，或读不出它的形态（例如侧挂的悬挂牌）。
    UnknownSignShape,
    /// 附着的那一格压根不是容器。
    NotAContainer,
    /// 是容器，但类型不在 `shopContainers` 白名单里（服主配置问题）。
    NotAllowedContainer,
    /// 容器是空的：没有商品可卖。
    Empty,
    /// 这个容器已经被另一块合法商店牌绑定了。
    AlreadyBound,
};

struct ContainerLookup {
    /// 找到的容器（双箱已合并）。定位成功时就有值，即使 `error` 是
    /// `Empty`/`AlreadyBound`——这两种情况下容器本身是存在的。
    std::optional<ContainerRef> ref;
    ContainerError              error = ContainerError::None;
    /// 商品（第一个非空槽位的物品）的展示名；容器为空时是空串。
    std::string itemName;
    /// 商店箱里与商品**严格匹配**的总件数（双箱合并；类型 + aux + 用户数据），
    /// 空容器时为 0。可成交份数 = `itemCount / 一份的件数`（见 `stockUnits`）。
    std::int64_t itemCount = 0;

    [[nodiscard]] bool ok() const { return error == ContainerError::None && ref.has_value(); }
};

struct ContainerLookupOptions {
    /// 可作商店库存的方块类型（`minecraft:chest` 这类带命名空间的 ID）。
    std::vector<std::string> const& allowedBlockTypes;
    /// 判断「这个容器已经被别的商店牌绑定了」需要解析别人的牌面。
    ShopSignConfig const& signConfig;
};

/// 读出牌子的形态与朝向。不是告示牌、或判不出形态时返回空。
std::optional<SignRef> readSignGeometry(BlockSource& region, BlockPos const& signPos);

/// 引擎坐标 ↔ 纯逻辑坐标。两边的转换只在这里定义。
BlockPos toBlockPos(Cell const& cell);
Cell     toCell(BlockPos const& pos);

/// 找出这一格上的商店牌所附着的容器，并报告开店前需要知道的三件事：
/// 容器身份（双箱已合并）、它是否已被别的商店牌占用、第一个非空槽位是什么。
///
/// 只判断附着的那一格，不扫六面；`signPos` 是正在编辑的牌子，
/// 「已被别的牌绑定」的检查不会把它自己算成冲突。
ContainerLookup
inspectAttachedContainer(BlockSource& region, BlockPos const& signPos, ContainerLookupOptions const& options);

} // namespace ss::shop
