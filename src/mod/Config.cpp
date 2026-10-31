#include "mod/Config.h"

#include "ll/api/Config.h"
#include "ll/api/io/Logger.h"

#include <exception>

namespace ss::shop {
namespace {

bool isBlank(std::string const& text) { return text.find_first_not_of(" \t\r\n") == std::string::npos; }

/// @return 配置是否合法。不合法时已经打过错误日志。
bool validate(ShopConfig const& config, ll::io::Logger& logger) {
    if (config.maxShopAmount < 1) {
        logger.error("config.json 非法：maxShopAmount 必须 >= 1（当前 {}）", config.maxShopAmount);
        return false;
    }
    if (isBlank(config.adminShopName)) {
        logger.error("config.json 非法：adminShopName 不能为空");
        return false;
    }
    if (config.shopInteractionCooldownMs < 0) {
        logger.error(
            "config.json 非法：shopInteractionCooldownMs 不能为负数（当前 {}）",
            config.shopInteractionCooldownMs
        );
        return false;
    }
    if (config.shopContainers.empty()) {
        logger.error("config.json 非法：shopContainers 至少要有一个方块类型");
        return false;
    }
    for (auto const& type : config.shopContainers) {
        if (isBlank(type)) {
            logger.error("config.json 非法：shopContainers 里有空字符串");
            return false;
        }
    }
    return true;
}

} // namespace

std::optional<ShopConfig> loadShopConfig(std::filesystem::path const& path, ll::io::Logger& logger) {
    ShopConfig config;
    try {
        if (!ll::config::loadConfig(config, path)) {
            logger.error("config.json 解析失败：{}", path.string());
            return std::nullopt;
        }
    } catch (std::exception const& e) {
        logger.error("config.json 读取失败：{}（{}）", path.string(), e.what());
        return std::nullopt;
    }

    if (!validate(config, logger)) {
        return std::nullopt;
    }

    logger.info(
        "配置已加载：每份最多 {} 件，可作容器的方块 {} 种，管理员店名 \"{}\"",
        config.maxShopAmount,
        config.shopContainers.size(),
        config.adminShopName
    );
    return config;
}

ShopSignConfig toSignConfig(ShopConfig const& config) {
    return ShopSignConfig{
        config.maxShopAmount,
        config.adminShopName,
        config.blockSellPriceHigherThanBuy,
    };
}

} // namespace ss::shop
