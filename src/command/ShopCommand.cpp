#include "command/ShopCommand.h"

#include "util/log/Debug.h"
#include "core/economy/Service.h"
#include "event/HookSlots.h"
#include "util/log/Files.h"
#include "Shop.h"
#include "util/i18n/TextRender.h"
#include "core/trade/TradeService.h"

#include "ll/api/command/CommandHandle.h"
#include "ll/api/command/CommandRegistrar.h"
#include "ll/api/mod/Mod.h"
#include "ll/api/mod/ModManagerRegistry.h"

#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/server/commands/CommandPermissionLevel.h"

#include <cstdio>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "ll/api/data/Version.h"

namespace ss::shop {

/// `/shop debug <value>` 的参数包。`on` / `off` 只是两个字符串，不引入枚举参数：
/// 命令层越薄，改起来越不怕碰引擎的枚举转换规则。
///
/// 必须放在**具名**命名空间里：`overload<T>()` 会用 `boost::pfr` 反射 `T` 的成员，
/// 内部要取一个同名的 `extern` 假对象，内部链接的类型过不了这一关（clang 会报
/// "used but not defined ... does not have linkage"）。
struct DebugParam {
    std::string value;
};

namespace {

/// `Manifest::version` 是可选的：发布包一定会写，开发期手拼的 manifest 可能没有，
/// 所以读不到就如实写「未知」。
std::string versionText(std::optional<ll::data::Version> const& version) {
    return version.has_value() ? version->to_string() : ui::plain(text::keyed("shop.command.status.unknown"));
}

std::string const& selfVersion() {
    static std::string const version = versionText(Shop::getInstance().getSelf().getManifest().version);
    return version;
}

/// iListenAttentively 的版本。拿不到就如实写「未知」——它是硬依赖，但"版本号读不到"
/// 与"没装"是两件事，不该在这里编一个数字。
std::string dependencyVersion(std::string_view name) {
    auto& registry = ll::mod::ModManagerRegistry::getInstance();
    if (!registry.hasMod(name)) {
        return ui::plain(text::keyed("shop.command.status.not_installed"));
    }
    if (auto mod = registry.getMod(name)) {
        return versionText(mod->getManifest().version);
    }
    return ui::plain(text::keyed("shop.command.status.unknown"));
}

/// 浮点配置的可读写法：`0.0` 显示成 `0`，`0.1` 还是 `0.1`。
///
/// `std::to_string` 只给 6 位小数：`0.0000001` 会变成 `"0.000000"`，去零之后是 `"0"`——
/// 看起来像「压根没配」。所以去零后成了 `"0"` 而原值非 0 时，改用 `%.3g` 保住有效数字。
std::string decimalText(double value) {
    std::string text = std::to_string(value);
    if (text.find('.') != std::string::npos) {
        text.erase(text.find_last_not_of('0') + 1);
        if (!text.empty() && text.back() == '.') {
            text.pop_back();
        }
    }
    if (text == "0" && value != 0.0) {
        char buffer[32]{};
        std::snprintf(buffer, sizeof(buffer), "%.3g", value);
        return std::string{buffer};
    }
    return text;
}

/// Hook 表：全挂上时只报一行「已挂上 N/M」，异常的才逐条展开。
///
/// 十条 `[x]` 是日常噪音——`/shop status` 平时是拿来查病的，正常的那几条不值得占行。
/// 但**不能整段省掉**：配置坏掉时 hook 表恰恰是唯一能说明「到底挂上了没有」的东西
/// （那时交易与全部保护都没生效），所以汇总行永远在。
void appendHookLines(std::vector<Text>& lines, Text title, std::span<HookSlot const> slots) {
    std::size_t attached = 0;
    for (auto const& slot : slots) {
        if (slot.attached) {
            ++attached;
        }
    }

    lines.push_back(text::keyed("shop.command.status.hooks", Tone::Default, {
        std::move(title),
        text::lit(std::to_string(attached)),
        text::lit(std::to_string(slots.size())),
    }));

    for (auto const& slot : slots) {
        auto name = text::lit(std::string{slot.name});
        // 返回码只在非 0 时才提：`attach` 返回 0 是常态，十行都挂一句
        // 「返回值不代表成功」只会把真正异常的那一条淹掉。
        if (slot.attached) {
            if (slot.lastReturn != 0) {
                lines.push_back(text::keyed("shop.command.status.hook.attached_dirty", Tone::Default, {
                    std::move(name),
                    text::lit(std::to_string(slot.lastReturn)),
                }));
            }
        } else if (slot.attempted) {
            // `detachHooks` 会把 attached 置回 false：这里必须能区分「挂过又解开」与「从没试过」。
            lines.push_back(text::keyed("shop.command.status.hook.detached", Tone::Default, {
                std::move(name),
                text::lit(std::to_string(slot.lastReturn)),
            }));
        } else {
            lines.push_back(text::keyed("shop.command.status.hook.never", Tone::Default, {std::move(name)}));
        }
    }
}

/// 显示用路径：把 `\` 换成 `/`。
///
/// `getPluginDir()` 给的是 `./mods/Shop`（正斜杠），`/ "logs"` 拼出来的尾部却是平台
/// 分隔符，于是同一条路径里两种斜杠混着出现（`./mods/Shop\logs`），看着像坏了。
/// 正斜杠在 Windows 的资源管理器、`cd`、`ls` 里都认。
std::string displayPath(std::filesystem::path const& path) {
    auto text = path.string();
    std::replace(text.begin(), text.end(), '\\', '/');
    return text;
}

std::string boolText(bool value) {
    return value ? "true" : "false";
}

/// 一条分隔行。命令反馈通道是单色的（`§` 码会被连字符一起吞掉），所以层次只能
/// 靠这个和缩进做，不靠颜色——与表单那条路不同，别指望 `Tone` 在这里有视觉效果。
Text section(Text title) {
    return text::keyed("shop.command.section", Tone::Default, {std::move(title)});
}

void reportStatus(CommandOutput& output) {
    auto const& config = Shop::getInstance().config();

    std::vector<Text> lines;

    lines.push_back(text::keyed("shop.command.status.version", Tone::Default, {
        text::lit(selfVersion()),
        text::lit(dependencyVersion("iListenAttentively")),
    }));

    lines.push_back(section(text::keyed("shop.command.section.config")));

    // **配置坏了不是「没什么可看」**：这时交易与全部保护都没生效，而 hook 表恰恰是
    // 唯一能说明「到底挂上了没有」的东西。所以只跳过依赖配置的那几行，后面照打。
    if (!config) {
        lines.push_back(text::keyed("shop.command.status.config_unavailable"));
    } else {
        lines.push_back(text::keyed("shop.command.status.config.limits", Tone::Default, {
            text::lit(std::to_string(config->maxShopAmount)),
            text::lit(boolText(config->allowPartialTransactions)),
            text::lit(boolText(config->ignoreCreativeMode)),
        }));
        lines.push_back(text::keyed("shop.command.status.config.owner", Tone::Default, {
            text::lit(boolText(config->allowOwnerOpenChest)),
            text::lit(boolText(config->economy.requireOwnerOnline)),
        }));
        lines.push_back(text::keyed("shop.command.status.config.tax", Tone::Default, {
            text::lit(decimalText(config->tax.shopTaxRate)),
            text::lit(std::to_string(config->shopInteractionCooldownMs)),
        }));
        // 禁售表是一个“安静地拦人”的配置：写错了、或漏了一条，平时看不出任何异样，
        // 所以把生效条数摆在这里（写错正则会拒绝加载，不会带着半份表跑）。
        lines.push_back(text::keyed("shop.command.status.config.banned", Tone::Default, {
            text::lit(std::to_string(Shop::getInstance().banList().size())),
        }));
    }

    lines.push_back(section(text::keyed("shop.command.section.economy")));

    // **只读**：不在这里触发解析。解析由 `ServerStartedEvent` 或第一次动钱负责，
    // 敲一条查询命令不该顺手在记分板上建目标。
    //
    // 「尚未解析」也分两种（世界未就绪 / 世界已就绪但解析还没跑），
    // 混成一句会让服主去查错方向——两句话的解释权在 `economyUnresolvedReason()`。
    auto const unresolved = economyUnresolvedReason();
    lines.push_back(text::keyed("shop.command.status.economy", Tone::Default, {
        ss::shop::text::empty(unresolved) ? text::lit(economySelectionText()) : unresolved,
    }));

    if (config) {
        lines.push_back(section(text::keyed("shop.command.section.log")));

        // 交易流水（`p1-transaction-log` D4）：两个配置项 + 现在到底开没开。
        // 「开」不等于「能写」：目录建不出来、磁盘满的时候每一笔都会 warn（同进程一次），
        // 所以这里只报**配置与目录**，不编一个健康状态。
        lines.push_back(text::keyed(
            config->transactionLog.compressAfterDays == 0 ? "shop.command.status.log.line.never"
                                                          : "shop.command.status.log.line",
            Tone::Default,
            config->transactionLog.compressAfterDays == 0
                ? TextArgs{
                      text::keyed(config->transactionLog.enabled ? "shop.command.status.log.on"
                                                                 : "shop.command.status.log.off"),
                  }
                : TextArgs{
                      text::keyed(config->transactionLog.enabled ? "shop.command.status.log.on"
                                                                 : "shop.command.status.log.off"),
                      text::lit(std::to_string(config->transactionLog.compressAfterDays)),
                  }
        ));
        lines.push_back(text::keyed("shop.command.status.log.dir", Tone::Default, {
            text::lit(displayPath(logDirectory())),
        }));
    }

    lines.push_back(section(text::keyed("shop.command.section.runtime")));

    // 冻结的交易对：`compensation_failed` 之后钱货没对上，交易被冻在这里等人工核对。
    // 注意这只是**当前进程**里的记录：加载器若整块卸载 DLL，静态对象会一起消失。
    // 0 是常态：那串「compensation_failed 之后……关服日志里会列出」只在真冻了才有用。
    auto const frozen = frozenTradeCount();
    lines.push_back(text::keyed(
        frozen == 0 ? "shop.command.status.frozen" : "shop.command.status.frozen.note",
        Tone::Default,
        {text::lit(std::to_string(frozen))}
    ));

    lines.push_back(text::keyed("shop.command.status.debug", Tone::Default, {
        text::keyed(shopDebugEnabled() ? "shop.command.status.debug.on" : "shop.command.status.debug.off"),
    }));

    lines.push_back(section(text::keyed("shop.command.section.hooks")));

    appendHookLines(lines, text::keyed("shop.command.hooks.destruction"), destructionHookSlots());
    appendHookLines(lines, text::keyed("shop.command.hooks.content"), contentHookSlots());

    output.success(ss::shop::text::renderLines(lines));
}

void applyDebugSwitch(CommandOutput& output, std::string const& value) {
    // 配置不可用时模组没启用：改一个全局开关只会留下「看起来生效了」的假象。
    if (!Shop::getInstance().config()) {
        output.error(ui::line(ui::Tone::Failure, "shop.command.unavailable"));
        return;
    }
    if (value == "on") {
        setShopDebugEnabled(true);
        output.success(ui::line(ui::Tone::Success, "shop.command.debug.on"));
        return;
    }
    if (value == "off") {
        setShopDebugEnabled(false);
        output.success(ui::line(ui::Tone::Hint, "shop.command.debug.off"));
        return;
    }
    output.error(ui::line(ui::Tone::Hint, "shop.command.debug.usage"));
}

} // namespace

void registerShopCommand() {
    // 重载模组（`/ll reload Shop`）会再跑一遍 enable()。命令句柄是全局的，
    // 再挂一次 `overload` 会让一条 `/shop status` 执行两遍，所以只挂一次。
    static bool registered = false;
    if (registered) {
        return;
    }
    registered = true;

    auto& logger = Shop::getInstance().getSelf().getLogger();
    auto& command =
        ll::command::CommandRegistrar::getInstance(/*isClientSide=*/false)
            .getOrCreateCommand(
                "shop",
                ui::plain(text::keyed("shop.command.description")),
                CommandPermissionLevel::GameDirectors
            );

    command.overload().text("status").execute([](CommandOrigin const&, CommandOutput& output) {
        reportStatus(output);
    });

    command.overload<DebugParam>().text("debug").required("value").execute(
        [](CommandOrigin const&, CommandOutput& output, DebugParam const& param) {
            applyDebugSwitch(output, param.value);
        }
    );

    // `/shop reload`：只重读 `config.json`，不卸载模组、不重挂 hook。
    //
    // 与 `/ll reload Shop` 的分工：后者会把整个模组拆了重装（监听器、冻结名单、经济后端
    // 都要重新来过）；只想改一个数（价格上限、税率、开关）时不必付那个代价。
    // hook 里一律现取配置，所以换完下一次交互就用新值。
    command.overload().text("reload").execute([](CommandOrigin const&, CommandOutput& output) {
        if (auto const error = Shop::getInstance().reloadConfig()) {
            output.error(ui::line(ui::Tone::Failure, *error));
            return;
        }
        output.success(ui::line(ui::Tone::Success, "shop.command.reload.ok"));
    });

    // 注册本身没有返回值可判；这里只在拿不到命令句柄时留一条痕迹。
    logger.debug(ui::plain(text::keyed("shop.command.registered")));
}

} // namespace ss::shop
