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
        // `auto` 单独报：它是老默认值，删掉它的理由（会偷偷换账本）与「写错字」完全不同，
        // 只回一句「只能是 A / B / C」会让服主把老配置当成拼写错误去猜。
        if (config.economy.backend == "auto") {
            logger.error(
                "config.json 非法：economy.backend = \"auto\" 已被移除——它会让本模组在探不到 "
                "LegacyMoney 时悄悄改用记分板，账本换家必须由你决定。请明确写成 {} 之一后重启"
                "（用 LegacyMoney 就写 legacy_money；用记分板先建好目标再写 scoreboard）",
                economy::legalBackendNamesText(" / ")
            );
            return false;
        }
        logger.error(
            "config.json 非法：economy.backend 只能是 {}（当前 \"{}\"；builtin 是 P1，暂不可用）",
            economy::legalBackendNamesText(" / "),
            config.economy.backend
        );
        return false;
    }
    if (*backend == economy::Backend::Builtin) {
        // `builtin`（自建账本）是 P1。明确拒，而不是拿别的后端顶替：服主写了 builtin 就是
        // 不想要 LegacyMoney 与记分板，拿它们顶替会把余额落到一个他没在看的地方。
        logger.error(
            "config.json 非法：economy.backend = \"builtin\"（自建账本）是 P1 的计划，"
            "P0 请用 legacy_money / scoreboard / null"
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
    // `0` 是「永不压缩」的正式取值，负数没有意义（它会让 `today - days` 跑到未来，
    // 把所有文件都压掉）。不静默按 0 处理：服主写错了应该知道。
    if (config.transactionLog.compressAfterDays < 0) {
        logger.error(
            "config.json 非法：transactionLog.compressAfterDays 不能为负数（0 = 永不压缩，当前 {}）",
            config.transactionLog.compressAfterDays
        );
        return false;
    }
    return true;
}

} // namespace

std::optional<ShopConfig> loadShopConfig(std::filesystem::path const& path, ll::io::Logger& logger) {
    ShopConfig config;
    try {
        // loadConfig 返回 false 的含义是「文件缺 version 或内容为空，已经用默认值补齐到内存」，
        // 不是读失败：它只在抛出异常/JSON 语法错时算失败。手写一份只填几个键的 config.json
        // 属于前者（缺 version），不能因此判配置非法。
        if (!ll::config::loadConfig(config, path)) {
            logger.warn(
                "config.json 缺少 version 或为空，已按默认值补齐（本次按补齐后的配置运行）：{}",
                path.string()
            );
            // 回写一份补上 version 的完整文件，避免每次启动都重跑迁移。
            if (!ll::config::saveConfig(config, path)) {
                logger.warn("config.json 回写失败，下次启动仍会重新迁移默认值：{}", path.string());
            }
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
