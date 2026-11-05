#include "util/config/Config.h"

#include "util/i18n/TextRender.h"
#include "ll/api/Config.h"
#include "ll/api/io/FileUtils.h"
#include "ll/api/io/Logger.h"
#include "util/config/ConfigPatch.h"
#include "core/shop/ItemBanList.h"
#include "core/trade/TradePricing.h"

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
        logger.error(ui::msg("shop.log.config.max_amount_min", config.maxShopAmount));
        return false;
    }
    // 上界不是「拍脑袋的限制」：份数上限同时是一份的件数上限，而件数 = 份数 × 每份件数
    // 在表单里用 int 算（`UnitsForm` / `ShopForm`）。32768² = 1.07e9 < INT_MAX，所以
    // 只要它不超过这个数，表单层的乘法就不会溢出。再大只能靠 int64 兜，而牌面本身也会先变得不可读。
    constexpr int kMaxShopAmountLimit = 32'768;
    if (config.maxShopAmount > kMaxShopAmountLimit) {
        logger.error(ui::msg("shop.log.config.max_amount_max", kMaxShopAmountLimit, config.maxShopAmount)
        );
        return false;
    }
    if (isBlank(config.adminShopName)) {
        logger.error(ui::msg("shop.log.config.admin_name_blank"));
        return false;
    }
    if (config.shopInteractionCooldownMs < 0) {
        logger.error(ui::msg("shop.log.config.cooldown_min", config.shopInteractionCooldownMs)
        );
        return false;
    }
    // 上界：这个值填成 10 分钟，所有交易都会「点了没反应」，而且没有任何日志能提醒到服主。
    constexpr int kMaxCooldownMs = 60'000;
    if (config.shopInteractionCooldownMs > kMaxCooldownMs) {
        logger.error(ui::msg("shop.log.config.cooldown_max", kMaxCooldownMs, config.shopInteractionCooldownMs)
        );
        return false;
    }
    if (config.shopContainers.empty()) {
        logger.error(ui::msg("shop.log.config.containers_empty"));
        return false;
    }
    for (auto const& type : config.shopContainers) {
        if (isBlank(type)) {
            logger.error(ui::msg("shop.log.config.containers_blank"));
            return false;
        }
    }

    // 禁售物品表：正则写错就**拒绝加载**。丢一条禁售规则是静默失效——店主照样能卖服主
    // 明令禁掉的东西，而服主只会看到“我明明配了”。与税率那种“写错就不收税”不同，
    // 这里的默认方向必须是安全的那一侧。
    ItemBanList const banList{config.bannedItems};
    if (!banList.invalidPatterns().empty()) {
        logger.error(ui::msg("shop.log.config.banned_regex_invalid", joinKeys(banList.invalidPatterns())));
        return false;
    }
    for (auto const& pattern : config.bannedItems) {
        // 只写名字、不写命名空间是最常见的一处错：整串匹配下 `bedrock` 匹配不到
        // `minecraft:bedrock`，而这条规则会静静地什么也不拦。不拒绝（正则本来就允许这么写），
        // 但得说一句。带通配符的规则（`.*shulker_box`）不提醒：服主显然知道自己在写正则。
        bool const hasMetaChars = pattern.find_first_of(".[]{}()*+?\\|^$") != std::string::npos;
        if (!isBlank(pattern) && !hasMetaChars && pattern.find(':') == std::string::npos) {
            logger.warn(ui::msg("shop.log.config.banned_no_namespace", pattern));
        }
    }
    auto const backend = economy::parseBackend(config.economy.backend);
    if (!backend) {
        // `auto` 单独报：它是老默认值，删掉它的理由（会偷偷换账本）与「写错字」完全不同，
        // 只回一句「只能是 A / B / C」会让服主把老配置当成拼写错误去猜。
        if (config.economy.backend == "auto") {
            logger.error(ui::msg("shop.log.config.backend_auto_removed", economy::legalBackendNamesText(" / "))
            );
            return false;
        }
        logger.error(ui::msg(
                "shop.log.config.backend_invalid",
                economy::legalBackendNamesText(" / "),
                config.economy.backend
            )
        );
        return false;
    }
    if (*backend == economy::Backend::Builtin) {
        // `builtin`（自建账本）是 P1。明确拒，而不是拿别的后端顶替：服主写了 builtin 就是
        // 不想要 LegacyMoney 与记分板，拿它们顶替会把余额落到一个他没在看的地方。
        logger.error(ui::msg("shop.log.config.backend_builtin"));
        return false;
    }
    if (isBlank(config.economy.scoreboardObjective)) {
        logger.error(ui::msg("shop.log.config.objective_blank"));
        return false;
    }
    // 税率是百分比：0 <= rate < 1。>= 1 等于全额征税（店主一分不收），负税率会变成「给店主发钱」，
    // NaN 会让每一次比较都为假；精度只到千分位（0.001 = 0.1%），更细的粒度在小额交易上
    // 本来就会被「税额下限 1」吃掉。
    //
    // **但这几项都不拒绝加载**：一个税率数字写错，不该让交易、商店箱保护、破坏防护全部关掉。
    // 处理方式是「这个配置失效」——WARN 一次，本次运行按 0 处理，其余照常。
    if (!(config.tax.shopTaxRate >= 0.0) || config.tax.shopTaxRate >= 1.0) {
        logger.warn(ui::msg("shop.log.config.tax_off_grid", config.tax.shopTaxRate));
        config.tax.shopTaxRate = 0.0;
    } else if (!ss::shop::isTaxRateOnGrid(config.tax.shopTaxRate)) {
        logger.warn(ui::msg("shop.log.config.tax_precision", config.tax.shopTaxRate));
        config.tax.shopTaxRate = 0.0;
    }
    // `0` 是「永不压缩」的正式取值，负数没有意义（它会让 `today - days` 跑到未来，
    // 把所有文件都压掉）。不静默按 0 处理：服主写错了应该知道。
    if (config.transactionLog.compressAfterDays < 0) {
        logger.error(ui::msg("shop.log.config.compress_negative", config.transactionLog.compressAfterDays)
        );
        return false;
    }
    // 上界：`std::chrono::days` 的 rep 是整数，填一个极大的值会在 `today - days` 处溢出。
    constexpr int kMaxCompressAfterDays = 3'650;
    if (config.transactionLog.compressAfterDays > kMaxCompressAfterDays) {
        logger.error(ui::msg(
                "shop.log.config.compress_max",
                kMaxCompressAfterDays,
                config.transactionLog.compressAfterDays
            )
        );
        return false;
    }
    // `tax.taxDestination` 目前只认 `void`（销毁）。写了别的值不会报错、也不会生效——
    // 那是最难查的一种配置（“我明明配了税收账户”）。这里只 warn、不拒绝加载：
    // 指向税收账户是 P1 的功能，不该因为将来才会有的一个键把整服卡住。
    if (config.tax.taxDestination != "void") {
        logger.warn(ui::msg("shop.log.config.tax_destination_ignored", config.tax.taxDestination));
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
            logger.error(ui::msg("shop.log.config.write_default_failed", path.string()));
            return std::nullopt;
        }
        logger.info(ui::msg("shop.log.config.created", path.string()));
    }

    auto const content = ll::file_utils::readFile(path);
    if (!content) {
        logger.error(ui::msg("shop.log.config.read_failed", path.string()));
        return std::nullopt;
    }

    // 语法坏的配置文件不覆盖：那可能是服主正在手改，盖掉等于把他的内容吃掉。
    // 能解析但缺字段是另一回事（下面一律用默认值补），不必弄死整个模组。
    nlohmann::ordered_json user;
    try {
        user = nlohmann::ordered_json::parse(*content, nullptr, /*allow_exceptions=*/true, /*ignore_comments=*/true);
    } catch (std::exception const& e) {
        logger.error(ui::msg("shop.log.config.parse_failed", path.string(), e.what()));
        return std::nullopt;
    }
    if (!user.is_object()) {
        logger.error(ui::msg("shop.log.config.not_object", path.string()));
        return std::nullopt;
    }

    auto const defaultsJson = ll::reflection::serialize<nlohmann::ordered_json>(defaults);
    if (!defaultsJson) {
        logger.error(ui::msg("shop.log.config.serialize_failed", defaultsJson.error().message()));
        return std::nullopt;
    }

    // 拼错的键先报：它们既不会生效也不报错，比缺键难查得多。
    if (auto const unknown = unknownConfigKeys(*defaultsJson, user); !unknown.empty()) {
        logger.warn(ui::msg("shop.log.config.unknown_keys", unknown.size(), joinKeys(unknown)));
    }
    if (user.contains("version") && user["version"].is_number_integer()
        && user["version"].get<std::int64_t>() > defaults.version) {
        logger.warn(ui::msg(
                "shop.log.config.version_newer",
                user["version"].get<std::int64_t>(),
                defaults.version
            )
        );
    }

    // **无条件**补齐：默认值打底、用户值覆盖。
    // 不能指望 `ll::config::loadConfig`：它只在文件缺 `version` 或 version 与代码不同时
    // 才 merge 默认值，否则直接严格反序列化——少一个键就抛 `missing required field`，
    // 整个模组拒绝加载。那等于「每加一个配置字段都必须记得 bump version」，忘了就全服起不来。
    auto const merged = mergeConfigDefaults(*defaultsJson, user);

    auto config = ll::reflection::deserialize_to<ShopConfig>(merged);
    if (!config) {
        logger.error(ui::msg("shop.log.config.deserialize_failed", path.string(), config.error().message()));
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
            logger.warn(ui::msg("shop.log.config.backup_failed", backupEc.message()));
        }

        if (!ll::config::saveConfig(*config, path)) {
            logger.warn(ui::msg("shop.log.config.rewrite_failed", path.string()));
        } else if (!missing.empty()) {
            logger.info(ui::msg("shop.log.config.rewritten_missing", joinKeys(missing), backup.string(), path.string())
            );
        } else {
            logger.info(ui::msg("shop.log.config.rewritten_version", config->version, backup.string(), path.string())
            );
        }
    }

    // 禁售规则条数现算一次：这里比 `Shop` 里那份缓存（还没建）更准，而且空规则不计入。
    ItemBanList const banList{config->bannedItems};
    logger.info(ui::msg(
            "shop.log.config.loaded",
            config->maxShopAmount,
            config->shopContainers.size(),
            banList.size(),
            config->adminShopName,
            config->economy.backend,
            config->tax.shopTaxRate,
            ui::plain(text::keyed(config->transactionLog.enabled ? "shop.log.config.log.on" : "shop.log.config.log.off"))
        )
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
