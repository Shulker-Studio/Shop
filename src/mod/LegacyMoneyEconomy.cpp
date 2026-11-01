#include "mod/LegacyMoneyEconomy.h"

#include "mod/Shop.h"

#include <windows.h>

#include <limits>
#include <type_traits>
#include <utility>

namespace ss::shop {
namespace {

/// 模块名。x64 的导出是未修饰的 C 名（`LLMoney_Get` 而不是 `_LLMoney_Get`）。
constexpr wchar_t const* kModuleName = L"LegacyMoney.dll";

/// 各导出的名字。写错一个就是「找不到导出」，启动时就能看出来。
constexpr char const* kGetExport    = "LLMoney_Get";
constexpr char const* kSetExport    = "LLMoney_Set";
constexpr char const* kAddExport    = "LLMoney_Add";
constexpr char const* kReduceExport = "LLMoney_Reduce";
constexpr char const* kTransExport  = "LLMoney_Trans";

/// `LLMoney.h` 里的金额是 `long long`，我们内部统一 `std::int64_t`；Windows x64 上两者同宽同 ABI。
static_assert(std::is_same_v<long long, std::int64_t>, "LegacyMoney 的 long long 与 int64 必须是同一类型");

template <class Fn>
Fn resolveExport(HMODULE module, char const* name) {
    return reinterpret_cast<Fn>(GetProcAddress(module, name));
}

} // namespace

LegacyMoneyEconomy::LegacyMoneyEconomy(Exports exports) : mExports(exports) {}

std::shared_ptr<LegacyMoneyEconomy> LegacyMoneyEconomy::create(economy::ProbeIssue& issue) {
    // 顺序就是 DESIGN §8.4：先看模块在不在，再看导出齐不齐。
    HMODULE const module = GetModuleHandleW(kModuleName);
    if (module == nullptr) {
        issue = economy::ProbeIssue::ModuleMissing;
        return nullptr;
    }

    Exports const exports{
        resolveExport<GetFn>(module, kGetExport),
        resolveExport<SetFn>(module, kSetExport),
        resolveExport<AddFn>(module, kAddExport),
        resolveExport<ReduceFn>(module, kReduceExport),
        resolveExport<TransFn>(module, kTransExport),
    };
    if (exports.get == nullptr || exports.set == nullptr || exports.add == nullptr || exports.reduce == nullptr
        || exports.trans == nullptr) {
        // 有一种已知的版本差异：另一套 API 是 C++ 命名空间版（mangled 符号）。我们**不猜**
        // 那些名字（猜错就是把钱写进一个不该写的账户），直接判为「版本不兼容」并降级。
        issue = economy::ProbeIssue::ExportsMissing;
        return nullptr;
    }

    issue = economy::ProbeIssue::None;
    // 构造函数私有，所以用不了 `std::make_shared`（故意的：探测成功才允许有实例）。
    return std::shared_ptr<LegacyMoneyEconomy>{new LegacyMoneyEconomy(exports)};
}

bool LegacyMoneyEconomy::modulePresent() const {
    if (GetModuleHandleW(kModuleName) != nullptr) {
        return true;
    }
    if (!mMissingWarned) {
        mMissingWarned = true;
        Shop::getInstance().getSelf().getLogger().error(
            "LegacyMoney.dll 已经不在进程里了（被卸载？）：经济后端随之失效，"
            "余额一律读不到、转账一律拒绝"
        );
    }
    return false;
}

std::optional<std::int64_t> LegacyMoneyEconomy::get(economy::Account const& account) const {
    if (account.empty() || !modulePresent()) {
        return std::nullopt;
    }
    long long const money = mExports.get(account);
    // 负值一律当作「读不到」：`-1` 是 LegacyMoney 自己的错误哨兵（空 XUID / 数据库异常），
    // 而它的存储层根本造不出负余额——`LLMoney_Trans` 会拦住扣成负数，`Set` 也走同一条路。
    if (money < 0) {
        return std::nullopt;
    }
    return static_cast<std::int64_t>(money);
}

bool LegacyMoneyEconomy::set(economy::Account const& account, std::int64_t amount) {
    if (account.empty() || !modulePresent()) {
        return false;
    }
    return mExports.set(account, amount);
}

bool LegacyMoneyEconomy::add(economy::Account const& account, std::int64_t amount) {
    if (account.empty() || amount <= 0 || !modulePresent()) {
        return false;
    }
    // 这个后端内部是 `long long`：加法先在这里堵住溢出，别让 SQLite 那边算出个负数余额。
    auto const current = get(account);
    if (!current || *current > std::numeric_limits<std::int64_t>::max() - amount) {
        return false;
    }
    return mExports.add(account, amount);
}

void LegacyMoneyEconomy::warnNegativeOnce() const {
    if (mNegativeWarned) {
        return;
    }
    mNegativeWarned = true;
    // LegacyMoney 永远拒绝负余额（`LLMoney_Trans` 里先比余额再扣），只能按 false 处理。
    Shop::getInstance().getSelf().getLogger().debug(
        "LegacyMoney 不支持 allowNegative，已按 false 处理（商店不用这个参数）"
    );
}

bool LegacyMoneyEconomy::reduce(economy::Account const& account, std::int64_t amount, bool allowNegative) {
    if (account.empty() || amount <= 0 || !modulePresent()) {
        return false;
    }
    if (allowNegative) {
        warnNegativeOnce();
    }
    // `LLMoney_Reduce` 内部走 `LLMoney_Trans(xuid, "", amount)`：余额不足时整体回滚，余额一分不动。
    return mExports.reduce(account, amount);
}

economy::TransferResult LegacyMoneyEconomy::transfer(
    economy::Account const& from,
    economy::Account const& to,
    std::int64_t            amount,
    bool                    allowNegative
) {
    // 参数校验与文案与基类默认实现共用一份（金额/空付款方/自转自/系统账户）。
    if (auto const invalid = economy::checkTransfer(from, to, amount, supportsMoneySink())) {
        return *invalid;
    }
    if (allowNegative) {
        warnNegativeOnce();
    }
    if (!modulePresent()) {
        // 与「被拒绝」分开说：这一笔一分钱都没动，原因是后端整个不在了。
        return {economy::TransferStatus::Rejected, "LegacyMoney.dll 已经不在进程里了，这一笔没有动账"};
    }

    // 只走这一次 `LLMoney_Trans`：它内部是一个 SQLite 事务（扣款、入账、写流水一起提交或一起回滚），
    // 并且会自己应用 `pay_tax` 与 `ListenBeforeEvent` 的拦截。自己拼 Reduce + Add 会漏税或重复扣税。
    // 第 4 个参数是流水备注，商店暂时不写（P1 的流水功能再补）。
    if (!mExports.trans(from, to, amount, std::string{})) {
        return {
            economy::TransferStatus::Rejected,
            "LegacyMoney 拒绝了这笔转账（余额不足、被其他插件拦下，或它自己的数据库出错）"
        };
    }
    return {economy::TransferStatus::Ok, {}};
}

} // namespace ss::shop
