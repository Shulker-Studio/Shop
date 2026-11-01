#include "mod/Config.h"

#include "ll/api/Config.h"
#include "ll/api/io/Logger.h"

#include <exception>
#include <string_view>

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
    auto const backend = economy::parseBackend(config.economy.backend);
    if (!backend) {
        logger.error(
            "config.json 非法：economy.backend 只能是 {}（当前 \"{}\"；builtin 是 P1，暂不可用）",
            economy::legalBackendNamesText(" / "),
            config.economy.backend
        );
        return false;
    }
    if (*backend == economy::Backend::Builtin) {
        // `builtin`（自建账本）是 P1。明确拒，而不是悄悄当 auto 用：服主写了 builtin 就是
        // 不想要 LegacyMoney 与记分板，拿它们顶替会把余额落到一个他没在看的地方。
        logger.error(
            "config.json 非法：economy.backend = \"builtin\"（自建账本）是 P1 的计划，"
            "P0 请用 auto / legacy_money / scoreboard / null"
        );
        return false;
    }
    if (isBlank(config.economy.scoreboardObjective)) {
        logger.error("config.json 非法：economy.scoreboardObjective 不能为空");
        return false;
    }
    // 税率是百分比：0 <= rate < 1。>= 1 等于全额征税（店主一分不收），这种事不该靠
    // 舍入悄悄生效；负税率会变成「给店主发钱」。
    if (!(config.tax.shopTaxRate >= 0.0) || config.tax.shopTaxRate >= 1.0) {
        logger.error(
            "config.json 非法：tax.shopTaxRate 必须在 [0, 1) 之间（0.05 = 5%%，当前 {}）",
            config.tax.shopTaxRate
        );
        return false;
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
        "配置已加载：每份最多 {} 件，可作容器的方块 {} 种，管理员店名 \"{}\"，经济后端 {}，商店税 {}",
        config.maxShopAmount,
        config.shopContainers.size(),
        config.adminShopName,
        config.economy.backend,
        config.tax.shopTaxRate
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
