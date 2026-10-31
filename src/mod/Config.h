#pragma once

#include "shop/ShopSign.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace ll::io {
class Logger;
} // namespace ll::io

namespace ss::shop {

/// config.json 的内存形态。聚合体，字段名就是配置键名。
struct ShopConfig {
    /// 配置结构版本。`ll::config::loadConfig` 用它判断是否需要做默认值迁移，
    /// 所以它是契约的一部分，不能删。
    int                      version                     = 1;
    std::string              adminShopName               = "Admin Shop";
    std::vector<std::string> shopContainers              = {"minecraft:chest", "minecraft:trapped_chest"};
    int                      maxShopAmount               = 3456;
    bool                     blockSellPriceHigherThanBuy = true;
};

/// 读取并校验 config.json；文件不存在时按默认值写出一份。
///
/// 返回空表示配置非法（或无法解析）。调用方必须放弃启用，不能带着半份配置继续跑。
std::optional<ShopConfig> loadShopConfig(std::filesystem::path const& path, ll::io::Logger& logger);

/// 解析器与绑定判定只需要其中三项。
ShopSignConfig toSignConfig(ShopConfig const& config);

} // namespace ss::shop
