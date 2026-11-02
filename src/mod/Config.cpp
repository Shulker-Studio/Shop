#include "mod/Config.h"
#include "ll/api/Config.h"
#include "ll/api/io/FileUtils.h"
#include "ll/api/io/Logger.h"
#include "shop/ConfigPatch.h"
#include "shop/TradePricing.h"

#include <cmath>
#include <cstdint>
#include <exception>
#include <string_view>

namespace ss::shop {
namespace {

/// 把键列表拼成日志里的一行（`transactionLog、economy.backend`）。
std::string joinKeys(std::vector<std::string> const& keys) {
    std::string out;
    for (auto const& key : keys) {
        if (!out.empty()) {
            out += "、";
        }
        out += key;
    }
    return out;
}

bool isBlank(std::string const& text) { return text.find_first_not_of(" \t\r\n") == std::string::npos; }

/// @return 配置是否合法。不合法时已经打过错误日志。
bool validate(ShopConfig& config, ll::io::Logger& logger) {
    if (config.maxShopAmount < 1) {
        logger.error("config.json 非法：maxShopAmount 必须 >= 1（当前 {}）", config.maxShopAmount);
        return false;
    }
    // 上界不是「拍脑袋的限制」：份数上限同时是一份的件数上限，而件数 = 份数 × 每份件数
    // 在表单里用 int 算（`UnitsForm` / `ShopForm`）。32768² = 1.07e9 < INT_MAX，所以
    // 只要它不超过这个数，表单层的乘法就不会溢出。再大只能靠 int64 兜，而牌面本身也会先变得不可读。
    constexpr int kMaxShopAmountLimit = 32'768;
    if (config.maxShopAmount > kMaxShopAmountLimit) {
        logger.error(
            "config.json 非法：maxShopAmount 不能大于 {}（当前 {}）——它同时限制「一份的件数」，"
            "再大就会让份数 × 件数溢出",
            kMaxShopAmountLimit,
            config.maxShopAmount
        );
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
    // 上界：这个值填成 10 分钟，所有交易都会「点了没反应」，而且没有任何日志能提醒到服主。
    constexpr int kMaxCooldownMs = 60'000;
    if (config.shopInteractionCooldownMs > kMaxCooldownMs) {
        logger.error(
            "config.json 非法：shopInteractionCooldownMs 不能大于 {}（当前 {}）——再大等于把所有交易静默拦下",
            kMaxCooldownMs,
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
    // 税率是百分比：0 <= rate < 1。>= 1 等于全额征税（店主一分不收），负税率会变成「给店主发钱」，
    // NaN 会让每一次比较都为假；精度只到千分位（0.001 = 0.1%），更细的粒度在小额交易上
    // 本来就会被「税额下限 1」吃掉。
    //
    // **但这几项都不拒绝加载**：一个税率数字写错，不该让交易、商店箱保护、破坏防护全部关掉。
    // 处理方式是「这个配置失效」——WARN 一次，本次运行按 0 处理，其余照常。
    if (!(config.tax.shopTaxRate >= 0.0) || config.tax.shopTaxRate >= 1.0) {
        logger.warn(
            "config.json 里的 tax.shopTaxRate 不合法（必须在 [0, 1) 之间，0.05 = 5%%，当前 {}）："
            "本次运行按 0 处理（不收商店税），交易与保护照常",
            config.tax.shopTaxRate
        );
        config.tax.shopTaxRate = 0.0;
    } else if (!ss::shop::isTaxRateOnGrid(config.tax.shopTaxRate)) {
        logger.warn(
            "config.json 里的 tax.shopTaxRate 精度超过千分位（只支持 0.001 = 0.1%%，当前 {}）："
            "本次运行按 0 处理（不收商店税），交易与保护照常",
            config.tax.shopTaxRate
        );
        config.tax.shopTaxRate = 0.0;
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
    // 上界：`std::chrono::days` 的 rep 是整数，填一个极大的值会在 `today - days` 处溢出。
    constexpr int kMaxCompressAfterDays = 3'650;
    if (config.transactionLog.compressAfterDays > kMaxCompressAfterDays) {
        logger.error(
            "config.json 非法：transactionLog.compressAfterDays 不能大于 {}（当前 {}）",
            kMaxCompressAfterDays,
            config.transactionLog.compressAfterDays
        );
        return false;
    }
    // `tax.taxDestination` 目前只认 `void`（销毁）。写了别的值不会报错、也不会生效——
    // 那是最难查的一种配置（“我明明配了税收账户”）。这里只 warn、不拒绝加载：
    // 指向税收账户是 P1 的功能，不该因为将来才会有的一个键把整服卡住。
    if (config.tax.taxDestination != "void") {
        logger.warn(
            "config.json 里的 tax.taxDestination = \"{}\" 目前不生效（只支持 \"void\" = 销毁）："
            "指向税收账户是 P1 的功能，本次启动仍按销毁处理",
            config.tax.taxDestination
        );
    }
    return true;
}

} // namespace

std::optional<ShopConfig> loadShopConfig(std::filesystem::path const& path, ll::io::Logger& logger) {
    ShopConfig const defaults; // 结构默认值 = 这一版配置的完整形状

    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        // 首次运行：写一份完整的默认配置。
        if (!ll::config::saveConfig(defaults, path)) {
            logger.error("config.json 写不出来，无法生成默认配置：{}", path.string());
            return std::nullopt;
        }
        logger.info("config.json 不存在，已按默认值生成：{}", path.string());
    }

    auto const content = ll::file_utils::readFile(path);
    if (!content) {
        logger.error("config.json 读取失败（权限或磁盘）：{}", path.string());
        return std::nullopt;
    }

    // 语法坏的配置文件不覆盖：那可能是服主正在手改，盖掉等于把他的内容吃掉。
    // 能解析但缺字段是另一回事（下面一律用默认值补），不必弄死整个模组。
    nlohmann::ordered_json user;
    try {
        user = nlohmann::ordered_json::parse(*content, nullptr, /*allow_exceptions=*/true, /*ignore_comments=*/true);
    } catch (std::exception const& e) {
        logger.error("config.json 不是合法 JSON，保持原样不动：{}（{}）", path.string(), e.what());
        return std::nullopt;
    }
    if (!user.is_object()) {
        logger.error("config.json 顶层必须是一个对象：{}", path.string());
        return std::nullopt;
    }

    auto const defaultsJson = ll::reflection::serialize<nlohmann::ordered_json>(defaults);
    if (!defaultsJson) {
        logger.error("默认配置序列化失败（模组内部错误）：{}", defaultsJson.error().message());
        return std::nullopt;
    }

    // 拼错的键先报：它们既不会生效也不报错，比缺键难查得多。
    if (auto const unknown = unknownConfigKeys(*defaultsJson, user); !unknown.empty()) {
        logger.warn("config.json 里有 {} 个不认识的键，已忽略：{}", unknown.size(), joinKeys(unknown));
    }
    if (user.contains("version") && user["version"].is_number_integer()
        && user["version"].get<std::int64_t>() > defaults.version) {
        logger.warn(
            "config.json 的 version（{}）比本模组（{}）新：可能来自更新的版本，本次按本模组的字段解析",
            user["version"].get<std::int64_t>(),
            defaults.version
        );
    }

    // **无条件**补齐：默认值打底、用户值覆盖。
    // 不能指望 `ll::config::loadConfig`：它只在文件缺 `version` 或 version 与代码不同时
    // 才 merge 默认值，否则直接严格反序列化——少一个键就抛 `missing required field`，
    // 整个模组拒绝加载。那等于「每加一个配置字段都必须记得 bump version」，忘了就全服起不来。
    auto const merged = mergeConfigDefaults(*defaultsJson, user);

    auto config = ll::reflection::deserialize_to<ShopConfig>(merged);
    if (!config) {
        logger.error("config.json 反序列化失败：{}（{}）", path.string(), config.error().message());
        return std::nullopt;
    }

    if (!validate(*config, logger)) {
        return std::nullopt;
    }

    // 对齐到千分位网格：`validate` 已经确认它是 0.001 的整数倍，但二进制浮点下
    // `0.001` 存的是 0.00100000000000000002...。对齐一次，让计价、状态输出与流水
    // 用的是同一个值，而不是三个各自带浮点尾巴的近似。
    config->tax.shopTaxRate =
        std::round(config->tax.shopTaxRate / ss::shop::kTaxRateStep) * ss::shop::kTaxRateStep;

    // 只在「缺键 / version 不对」时回写。不用 `merged != user`：那个条件还会被
    // 「用户写了 null」触发（merge 后变成默认值），而那种文件不必重写。
    //
    // 回写本身就是一次重排：注释、键序、格式都会丢，所以先留一份 `.bak`。
    // 注意：回写序列化的是 `ShopConfig`，**不认识的键不会跟着回去**——如果这次回写
    // 恰好发生在「缺键 + 拼错键」同时存在的文件上，那个拼错的键会消失（上面已 WARN）。
    auto const missing      = missingConfigKeys(*defaultsJson, user);
    bool const versionStale = !user.contains("version") || !user["version"].is_number_integer()
                           || user["version"].get<std::int64_t>() != defaults.version;
    if (!missing.empty() || versionStale) {
        auto const      backup = std::filesystem::path{path.string() + ".bak"};
        std::error_code backupEc;
        std::filesystem::copy_file(path, backup, std::filesystem::copy_options::overwrite_existing, backupEc);
        if (backupEc) {
            logger.warn("config.json 回写前的备份失败：{}（本次仍会回写）", backupEc.message());
        }

        if (!ll::config::saveConfig(*config, path)) {
            logger.warn("config.json 回写失败，本次按补齐后的配置运行，下次启动会再补：{}", path.string());
        } else if (!missing.empty()) {
            logger.info(
                "config.json 已补齐并回写（补上默认值：{}）；原文件备份在 {}：{}",
                joinKeys(missing),
                backup.string(),
                path.string()
            );
        } else {
            logger.info(
                "config.json 已按本版结构回写（version = {}）；原文件备份在 {}：{}",
                config->version,
                backup.string(),
                path.string()
            );
        }
    }

    logger.info(
        "配置已加载：每份最多 {} 件，可作容器的方块 {} 种，管理员店名 \"{}\"，经济后端 {}，商店税 {}，交易流水 {}",
        config->maxShopAmount,
        config->shopContainers.size(),
        config->adminShopName,
        config->economy.backend,
        config->tax.shopTaxRate,
        config->transactionLog.enabled ? "开" : "关"
    );
    return std::move(*config);
}

ShopSignConfig toSignConfig(ShopConfig const& config) {
    return ShopSignConfig{
        config.maxShopAmount,
        config.adminShopName,
        config.blockSellPriceHigherThanBuy,
    };
}

} // namespace ss::shop
