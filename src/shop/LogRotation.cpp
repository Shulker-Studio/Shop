#include "shop/LogRotation.h"

#include <chrono>
#include <cstddef>

namespace ss::shop {
namespace {

/// 我们自己的流水文件后缀。`.jsonl.gz` 不以它结尾，所以「已经压过的」自然被排除。
constexpr std::string_view kLogExtension        = ".jsonl";
constexpr std::string_view kCompressedExtension = ".gz";

/// 日期在文件名里占的宽度（`YYYY-MM-DD`）与它前面那个分隔符。
constexpr std::size_t kDateLength = 10;

/// `YYYY-MM-DD` → 天数。长度、分隔符、数字、以及「2026-13-01」「2026-02-30」这样的假日期
/// 都返回空。用 `std::chrono::year_month_day` 判日历合法性，不自己算闰年。
std::optional<std::chrono::sys_days> parseDate(std::string_view text) {
    if (text.size() != kDateLength || text[4] != '-' || text[7] != '-') {
        return std::nullopt;
    }

    int         digits[8] = {}; // YYYYMMDD
    std::size_t at        = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (i == 4 || i == 7) {
            continue;
        }
        if (text[i] < '0' || text[i] > '9') {
            return std::nullopt;
        }
        digits[at++] = text[i] - '0';
    }

    int const      year  = digits[0] * 1000 + digits[1] * 100 + digits[2] * 10 + digits[3];
    unsigned const month = static_cast<unsigned>(digits[4] * 10 + digits[5]);
    unsigned const day   = static_cast<unsigned>(digits[6] * 10 + digits[7]);

    std::chrono::year_month_day const date{std::chrono::year{year}, std::chrono::month{month}, std::chrono::day{day}};
    if (!date.ok()) {
        return std::nullopt;
    }
    return std::chrono::sys_days{date};
}

} // namespace

std::string_view isoDateOf(std::string_view ts) {
    constexpr std::size_t kIsoDateLength = 10; // `YYYY-MM-DD`
    return ts.size() < kIsoDateLength ? ts : ts.substr(0, kIsoDateLength);
}

std::optional<std::string> logFileDate(std::string_view fileName) {
    if (!fileName.ends_with(kLogExtension)) {
        return std::nullopt;
    }
    auto const stem = fileName.substr(0, fileName.size() - kLogExtension.size());
    // 日期要么就是整个名字（新布局 `transactions/2026-11-01.jsonl`——类型已经由目录区分），
    // 要么前面还有一个 `-`（旧布局 `transactions-2026-11-01.jsonl`，搬家前后都可能遇到）。
    // 那个分隔符是必要的：`transactions2026-11-01.jsonl` 不该被认成自己的文件。
    if (stem.size() < kDateLength) {
        return std::nullopt;
    }
    if (stem.size() > kDateLength && stem[stem.size() - kDateLength - 1] != '-') {
        return std::nullopt;
    }
    auto const date = stem.substr(stem.size() - kDateLength);
    if (!parseDate(date)) {
        return std::nullopt;
    }
    return std::string{date};
}

bool shouldCompress(std::string_view fileDate, std::string_view today, int days) {
    if (days <= 0) {
        return false; // 0 = 永不压缩
    }
    auto const fileDay  = parseDate(fileDate);
    auto const todayDay = parseDate(today);
    if (!fileDay || !todayDay) {
        return false; // 认不出来的文件不动
    }
    // 严格早于「今天 − N 天」：正好 N 天的文件今天不压，N+1 天才压。
    return *fileDay < *todayDay - std::chrono::days{days};
}

std::optional<RelocatedLogFile> relocatedLogFile(std::string_view fileName) {
    struct Prefix {
        std::string_view prefix;
        std::string_view subdirectory;
    };
    // 顺序无所谓：两个前缀互不为前缀。
    constexpr Prefix kPrefixes[] = {
        {"transactions-", "transactions"},
        {"discarded-",    "discarded"   },
    };
    for (auto const& candidate : kPrefixes) {
        if (!fileName.starts_with(candidate.prefix)) {
            continue;
        }
        auto const rest = fileName.substr(candidate.prefix.size());
        // 去掉前缀后应当是 `YYYY-MM-DD.jsonl` 或 `YYYY-MM-DD.jsonl.gz`。
        // 这里不能直接复用 `logFileDate`：它要求日期**前面**有 `-`（那是为了不把
        // `transactions2026-11-01.jsonl` 认成自己的文件），而前缀已经在这之前被切掉了。
        auto const body =
            rest.ends_with(kCompressedExtension) ? rest.substr(0, rest.size() - kCompressedExtension.size()) : rest;
        if (!body.ends_with(kLogExtension)) {
            return std::nullopt;
        }
        auto const date = body.substr(0, body.size() - kLogExtension.size());
        if (!parseDate(date)) {
            return std::nullopt; // 日期不合法：不是我们写的（或者写坏了），不搬
        }
        return RelocatedLogFile{std::string{candidate.subdirectory}, std::string{rest}};
    }
    return std::nullopt;
}

std::string compressedLogFileName(std::string_view fileName) {
    if (fileName.ends_with(".gz")) {
        return std::string{fileName};
    }
    return std::string{fileName} + ".gz";
}

} // namespace ss::shop
