#include "mod/ShopCommand.h"

#include "mod/DebugLog.h"
#include "mod/EconomyService.h"
#include "mod/HookSlots.h"
#include "mod/LogFile.h"
#include "mod/Shop.h"
#include "mod/TradeService.h"

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
    return version.has_value() ? version->to_string() : std::string{"未知"};
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
        return "未安装";
    }
    if (auto mod = registry.getMod(name)) {
        return versionText(mod->getManifest().version);
    }
    return "未知";
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

std::string hookLines(std::string_view title, std::span<HookSlot const> slots) {
    std::string out{"hooks（"};
    out.append(title);
    out += "）：\n";
    for (auto const& slot : slots) {
        out += slot.attached ? "  [x] " : "  [ ] ";
        out += slot.name;
        if (slot.attached) {
            out += "  已挂上（attach 返回 " + std::to_string(slot.lastReturn) + "，返回值不代表成功）";
        } else if (slot.attempted) {
            // `detachHooks` 会把 attached 置回 false：这里必须能区分「挂过又解开」与「从没试过」。
            out += "  曾尝试挂上，现已解绑（上次返回 " + std::to_string(slot.lastReturn) + "）";
        } else {
            out += "  未尝试注册";
        }
        out += '\n';
    }
    return out;
}

void reportStatus(CommandOutput& output) {
    auto const& config = Shop::getInstance().config();

    std::string out{"Shop "};
    out += selfVersion();
    out += "（iListenAttentively ";
    out += dependencyVersion("iListenAttentively");
    out += "）\n";

    // **配置坏了不是「没什么可看」**：这时交易与全部保护都没生效，而 hook 表恰恰是
    // 唯一能说明「到底挂上了没有」的东西。所以只跳过依赖配置的那几行，后面照打。
    if (!config) {
        out += "配置：不可用（模组没启用：交易、商店箱保护、破坏防护、内容防盗全部没有生效）\n";
    } else {
        out += "配置：maxShopAmount=" + std::to_string(config->maxShopAmount);
        out += " allowPartialTransactions=" + std::string{config->allowPartialTransactions ? "true" : "false"};
        out += " ignoreCreativeMode=" + std::string{config->ignoreCreativeMode ? "true" : "false"};
        out += " allowOwnerOpenChest=" + std::string{config->allowOwnerOpenChest ? "true" : "false"};
        out += " requireOwnerOnline=" + std::string{config->economy.requireOwnerOnline ? "true" : "false"};
        out += " shopTaxRate=" + decimalText(config->tax.shopTaxRate);
        out += " shopInteractionCooldownMs=" + std::to_string(config->shopInteractionCooldownMs);
        out += " economy.backend=" + config->economy.backend;
        out += '\n';

        // 交易流水（`p1-transaction-log` D4）：两个配置项 + 现在到底开没开。
        // 「开」不等于「能写」：目录建不出来、磁盘满的时候每一笔都会 warn（同进程一次），
        // 所以这里只报**配置与目录**，不编一个健康状态。
        out += "交易流水：";
        out += config->transactionLog.enabled ? "开" : "关（业务流水与 discarded 一起停）";
        out += " compressAfterDays=" + std::to_string(config->transactionLog.compressAfterDays);
        out += config->transactionLog.compressAfterDays == 0 ? "（永不压缩）" : "（超期压成 .jsonl.gz，永不删除）";
        out += "，目录 " + transactionLogDirectory().string() + " 与 " + discardLogDirectory().string();
        out += '\n';
    }

    // **只读**：不在这里触发解析。解析由 `ServerStartedEvent` 或第一次动钱负责，
    // 敲一条查询命令不该顺手在记分板上建目标。
    out += "经济后端：";
    // 「尚未解析」也分两种（世界未就绪 / 世界已就绪但解析还没跑），
    // 混成一句会让服主去查错方向——两句话的解释权在 `economyUnresolvedReason()`。
    auto const unresolved = economyUnresolvedReason();
    out += unresolved.empty() ? economySelectionText() : unresolved;
    out += '\n';

    // 冻结的交易对：`compensation_failed` 之后钱货没对上，交易被冻在这里等人工核对。
    // 注意这只是**当前进程**里的记录：加载器若整块卸载 DLL，静态对象会一起消失。
    out += "冻结中的交易对：" + std::to_string(frozenTradeCount())
         + "（compensation_failed 后冻结，需人工核对；关服日志里会列出）";
    out += '\n';

    out += hookLines("破坏防护", destructionHookSlots());
    out += hookLines("内容防盗", contentHookSlots());

    out += "调试日志：";
    out += shopDebugEnabled() ? "开（/shop debug off 关闭）" : "关（/shop debug on 打开）";

    output.success(out);
}

void applyDebugSwitch(CommandOutput& output, std::string const& value) {
    // 配置不可用时模组没启用：改一个全局开关只会留下「看起来生效了」的假象。
    if (!Shop::getInstance().config()) {
        output.error("Shop 的配置不可用，模组没有启用（调试开关不生效）");
        return;
    }
    if (value == "on") {
        setShopDebugEnabled(true);
        output.success("调试日志已打开：每次牌面判定、保护拦截、交易结果、内容防盗拦截都会打一行 [shop/dbg]");
        return;
    }
    if (value == "off") {
        setShopDebugEnabled(false);
        output.success("调试日志已关闭");
        return;
    }
    output.error("用法：/shop debug <on|off>");
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
            .getOrCreateCommand("shop", "商店模组的状态查询与调试开关", CommandPermissionLevel::GameDirectors);

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
            output.error(*error);
            return;
        }
        output.success("配置已重新读取（不用重挂任何 hook，下一次交互就用新值）");
    });

    // 注册本身没有返回值可判；这里只在拿不到命令句柄时留一条痕迹。
    logger.debug("已注册 /shop 命令（status / debug / reload）");
}

} // namespace ss::shop
