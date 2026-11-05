#include "mod/LogFile.h"

#include "mod/Text.h"

#include "mod/Shop.h"
#include "mod/WallClock.h"
#include "shop/LogRotation.h"

#include "ll/api/io/Logger.h"

#include <zlib.h>

#include <cstdint>
#include <fstream>
#include <set>
#include <utility>
#include <string>
#include <system_error>
#include <vector>

namespace ss::shop {
namespace {

/// 已经建过的日志目录（进程级）。
///
/// `std::filesystem::create_directories` 每次调用都要逐级 `stat` + `CreateDirectoryW`，
/// 而日志只有两三个固定子目录——建过一次就不必再问。目录被外部删掉时 `open` 会失败，
/// 那一支会把记录抹掉并重来一次（见 `appendLogLines`）。
std::set<std::filesystem::path> gKnownDirs;

/// 已经提示过的失败（种类 + 路径）。
///
/// spec 要求「写失败只 warn（同进程一次）」：这几种失败一旦发生就是持续性的
/// （目录建不出来、文件打不开、磁盘仍是满的），只会每一笔交易都重复一遍同样一句话。
///
/// **键里必须有路径**：只按种类抑制的话，`transactions/` 写失败之后 `discarded/` 再失败
/// 就一声不响了；换了盘/改了权限之后的新失败也会被旧记录吃掉。
/// 进程级静态状态 + `resetLogWarnings()`（模组重载时清），见头文件。
///
/// `logFileDate` 现在只认裸日期（`YYYY-MM-DD.jsonl`），旧布局由 `relocatedLogFile` 单独处理。
std::set<std::pair<std::string, std::filesystem::path>> gWarnedFailures;

void warnOnce(std::string_view kind, std::filesystem::path const& path, std::string const& message) {
    // 键是 (种类, 路径) 这一对本身，不再拼成 `"种类|路径"` 的 UTF-8 字符串：
    // 拼一次要额外做一次宽字符 → UTF-8 的转换，而 `path` 本来就能直接比较。
    if (!gWarnedFailures.emplace(std::string{kind}, path).second) {
        return;
    }
    Shop::getInstance().getSelf().getLogger().warn(ui::msg("shop.log.logfile.warn_once", message));
}

/// 压缩用的缓冲。64 KiB：足够摊平每次 `gzwrite` 的调用开销，又不至于为一条日志占多少内存。
constexpr std::size_t kCopyBuffer = 64 * 1024;

/// 把一个文件 gzip 成 `finalPath`，**流式**读写（spec D4：不把整个文件读进内存——`discarded`
/// 里可能有几十 KB 的 SNBT，一天的流水可以到几十 MB）。
///
/// 顺序是「写 `.part` → 关好 → 改名为成品 → 删原文件」，每一步都为了同一件事：
/// **成品名只会在内容完整时出现**。
/// - 直接写成品名的话，中途崩服会留下一个截断的 `.gz`，下一轮看到「`.gz` 已存在」就当它压好了，
///   然后删掉原文件——这就是拿数据换磁盘；
/// - 改名前失败只留一个 `.part`（`logFileDate` 认不出它，扫描时天然跳过），下一轮重写；
/// - 删原文件放在最后，删不掉也只是留着两份，`compressLogFile` 下一轮补删。
///
/// @return 是否成功（成功 = `.gz` 写完并关好、已改名、原文件已删）。
bool gzipFile(std::filesystem::path const& source, std::filesystem::path const& finalPath) {
    auto const target = std::filesystem::path{finalPath.string() + ".part"};

    // 两个句柄都在这个作用域里，**出作用域先关掉**再删原文件：Windows 上不共享删除权限，
    // 自己还开着 `ifstream` 就删不掉——压缩会永远停在「`.gz` 写好了、原文件还在」。
    bool ok = true;
    {
        std::ifstream in(source, std::ios::binary);
        if (!in) {
            return false;
        }

#if defined(_WIN32)
        // Windows 上 `gzopen` 收窄字符串（系统代码页）：服主的目录名里有中文时就会打不开，
        // 所以走宽字符版本。别的平台用普通版本。
        gzFile out = gzopen_w(target.c_str(), "wb");
#else
        gzFile out = gzopen(target.string().c_str(), "wb");
#endif
        if (out == nullptr) {
            return false;
        }

        std::vector<char> buffer(kCopyBuffer);
        while (in) {
            in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            auto const read = in.gcount();
            if (read <= 0) {
                break;
            }
            // `gzwrite` 要的是 `unsigned` 长度；64 KiB 的块永远放得下。
            if (gzwrite(out, buffer.data(), static_cast<unsigned>(read)) != static_cast<int>(read)) {
                ok = false;
                break;
            }
        }
        if (in.bad()) {
            ok = false; // 读到一半出错：别把半个文件当成完整的
        }
        // `gzclose` 的返回值不能丢：压缩流的校验和与小尾巴是在它里面写出去的。
        if (gzclose(out) != Z_OK) {
            ok = false;
        }
    }
    std::error_code ec;
    if (!ok) {
        // 半成品不能留着：留着会让下一轮以为这一份已经压过。
        std::error_code cleanup;
        std::filesystem::remove(target, cleanup);
        return false;
    }

    std::filesystem::rename(target, finalPath, ec);
    if (ec) {
        std::error_code cleanup;
        std::filesystem::remove(target, cleanup);
        return false;
    }

    std::filesystem::remove(source, ec);
    // 删原文件失败也算失败：留着两个版本会让人以为压缩没生效（下一轮补删）。
    return !ec;
}

/// 压一个 `dir / fileName`。幂等：成品（`.jsonl.gz`）已经存在时只补「删原文件」这一步。
/// 返回 true = 这个文件名已经不需要再处理（压成功或原文件已删）。
bool compressLogFile(std::filesystem::path const& dir, std::string const& fileName) {
    auto const source = dir / fileName;
    auto const target = dir / compressedLogFileName(fileName);

    std::error_code ec;
    if (std::filesystem::exists(target, ec)) {
        // 成品在 = 上一次已经压成功（`.part` 只做中间态，从不会被当成成品），
        // 只是原文件没删掉（上一轮删的时候自己还开着，或者盘被拔了）。
        // 补上这一步，而不是永远留着两份——否则「压缩省磁盘」就成了空话。
        std::filesystem::remove(source, ec);
        if (ec) {
            warnOnce(
                "compress",
                source,
                ui::plain(text::keyed(
                    "shop.log.logfile.remove_old_failed",
                    ss::shop::Tone::Default,
                    {text::lit(source.string()), text::lit(ec.message())}
                ))
            );
            return false;
        }
        return true;
    }

    if (!gzipFile(source, target)) {
        // 失败不删原文件（`gzipFile` 保证），下次启动还会扫到它，重试是幂等的。
        warnOnce(
            "compress",
            source,
            ui::plain(text::keyed(
                "shop.log.logfile.compress_failed",
                ss::shop::Tone::Default,
                {text::lit(source.string())}
            ))
        );
        return false;
    }
    return true;
}

/// 压掉 `dir` 里过期的 `.jsonl`，返回真正处理了几个文件（压成功，或补删了原文件）。
int compressOneDirectory(std::filesystem::path const& dir, std::string const& today, int compressAfterDays) {
    std::error_code                     ec;
    std::filesystem::directory_iterator it(dir, ec);
    if (ec) {
        return 0; // 目录还不存在（第一次写之前）：没什么可压的，也不是失败
    }
    int compressed = 0;
    for (auto const& entry : it) {
        if (entry.is_directory(ec) || ec) {
            continue; // 子目录交给外层递归，这里只压本层的文件
        }
        auto const name = entry.path().filename().string();
        auto const date = logFileDate(name);
        if (!date || !shouldCompress(*date, today, compressAfterDays)) {
            continue;
        }
        if (compressLogFile(dir, name)) {
            ++compressed;
        }
    }
    return compressed;
}

/// 压掉两个**受控子目录**（`transactions/`、`discarded/`）里过期的文件。
///
/// **只在启动时调一次**（`compressOldLogsOnStartup`）。早先是在 `appendLogLines` 里写的：
/// 每落一行流水就把两个目录扫一遍，碰上过期文件还会当场做一次同步 gzip——
/// 那笔 CPU 与磁盘 I/O 全在「玩家刚成交」那一拍的 tick 线程上。压缩不是交易的一部分，
/// 也不需要那么准时：一天做一次就够，而启动时服主本来就在等，卡一下无人受损。
///
/// **不扫日志根目录**：根下是服主的地盘（旧布局残留、他自己放的备份）。
/// 以前会把 `backup-2026-11-01.jsonl` 当成自己的文件压掉并删掉原文件。
///
/// 两个目录都扫：销毁留档不该因为「没人卖东西」就永远等不到压缩。
int compressOldLogs(std::filesystem::path const& root, std::string const& today, int compressAfterDays) {
    if (compressAfterDays <= 0) {
        return 0; // 0 = 永不压缩：连目录都不必看
    }
    return compressOneDirectory(root / "transactions", today, compressAfterDays)
         + compressOneDirectory(root / "discarded", today, compressAfterDays);
}

} // namespace

std::filesystem::path logDirectory() {
    // `getModDir()` = `mods/<模组名>`，与配置目录（`mods/<模组名>/config`）平级。
    return Shop::getInstance().getSelf().getModDir() / "logs";
}

std::filesystem::path transactionLogDirectory() { return logDirectory() / "transactions"; }

std::filesystem::path discardLogDirectory() { return logDirectory() / "discarded"; }

void migrateLegacyLogs() {
    auto const root = logDirectory();

    std::error_code                     ec;
    std::filesystem::directory_iterator it(root, ec);
    if (ec) {
        return; // 还没有任何日志：没什么可搬的，也不是失败
    }

    auto&       logger = Shop::getInstance().getSelf().getLogger();
    std::size_t moved  = 0;
    for (auto const& entry : it) {
        std::error_code fileEc;
        if (!entry.is_regular_file(fileEc) || fileEc) {
            continue;
        }
        auto const target = relocatedLogFile(entry.path().filename().string());
        if (!target) {
            continue; // 不是本模组写的名字（或者已经是新布局）：不碰
        }

        auto const      dir = root / target->subdirectory;
        std::error_code mkdirEc;
        std::filesystem::create_directories(dir, mkdirEc);
        if (mkdirEc) {
            warnOnce(
                "migrate",
                dir,
                ui::plain(text::keyed(
                    "shop.log.logfile.migrate_mkdir_failed",
                    ss::shop::Tone::Default,
                    {text::lit(dir.string()), text::lit(mkdirEc.message())}
                ))
            );
            continue;
        }

        std::error_code renameEc;
        std::filesystem::rename(entry.path(), dir / target->fileName, renameEc);
        if (renameEc) {
            // 目标已存在（之前搬过一半）或文件被占用：留在原地，不覆盖。
            warnOnce(
                "migrate",
                entry.path(),
                ui::plain(text::keyed(
                    "shop.log.logfile.migrate_rename_failed",
                    ss::shop::Tone::Default,
                    {text::lit(entry.path().string()), text::lit(renameEc.message())}
                ))
            );
            continue;
        }
        ++moved;
    }

    if (moved > 0) {
        logger.info(ui::msg("shop.log.logfile.migrated", moved));
    }
}

bool appendLogLines(
    std::filesystem::path const&    dir,
    std::string const&              fileName,
    std::vector<std::string> const& lines
) {
    if (lines.empty()) {
        return true; // 没有东西要写：不算失败，也不必建目录
    }

    auto const* config = currentShopConfig();
    if (config == nullptr || !config->transactionLog.enabled) {
        // 总开关：两个文件一起停（业务流水与 `discarded` 都从这一个函数出去）。
        // 配置读不到（模组没启用）也当作关着——没有配置的时候没有「日志策略」可言。
        return false;
    }

    auto const path = dir / fileName;

    // 目录只建一次：`create_directories` 每次都要逐级 stat + CreateDirectoryW。
    // 打不开时多半是目录被外部删了——抹掉「已建」记录，第二轮重建再试一次。
    // `binary`：NDJSON 要求 `\n` 结尾，不能让文本模式把它翻成 `\r\n`。
    std::ofstream out;
    for (int attempt = 0; attempt < 2; ++attempt) {
        if (!gKnownDirs.contains(dir)) {
            std::error_code dirEc;
            std::filesystem::create_directories(dir, dirEc);
            if (dirEc) {
                warnOnce(
                    "create_directories",
                    dir,
                    ui::plain(text::keyed(
                        "shop.log.logfile.mkdir_failed",
                        ss::shop::Tone::Default,
                        {text::lit(dir.string()), text::lit(dirEc.message())}
                    ))
                );
                return false;
            }
            gKnownDirs.insert(dir);
        }
        out.clear();
        out.open(path, std::ios::binary | std::ios::app);
        if (out) {
            break;
        }
        gKnownDirs.erase(dir);
    }
    if (!out) {
        warnOnce(
            "open",
            path,
            ui::plain(text::keyed(
                "shop.log.logfile.open_failed",
                ss::shop::Tone::Default,
                {text::lit(path.string())}
            ))
        );
        return false;
    }

    // 回滚点：这一批开写之前文件有多大。
    //
    // 磁盘满 / 盘被拔掉时 `<<` 可能已经写进去半行，而 NDJSON 中间夹一条截断的 JSON
    // 会让**这一天整份流水**读不出来（`jq`、`duckdb read_json_auto` 都会在这一行失败）。
    // 所以失败时先把文件截回上一条完整记录的边界，再把这一批当成丢掉。
    //
    // 用「打开后跳到末尾再问位置」而不是 `std::filesystem::file_size`：后者在 Windows 上
    // 是另一次完整的 `CreateFileW` + `GetFileSizeEx` + `CloseHandle`（实测约 100 µs/笔），
    // 而 `seekp` + `tellp` 只是 `SetFilePointerEx`，几乎免费。
    out.seekp(0, std::ios::end);
    std::streampos const rollbackAt    = out.tellp();
    bool const           rollbackKnown = rollbackAt != std::streampos(-1);

    for (auto const& line : lines) {
        out << line;
    }
    // flush 之后作用域一结束就关文件：崩服时已成交的流水必须已经在盘上。
    out.flush();
    if (!out) {
        // 磁盘满、盘被拔掉都会走到这里。留一句痕迹，但**不**回头改交易结论。
        out.close();
        if (rollbackKnown) {
            std::error_code truncEc;
            std::filesystem::resize_file(
                path,
                static_cast<std::uintmax_t>(static_cast<std::streamoff>(rollbackAt)),
                truncEc
            );
            if (!truncEc) {
                warnOnce(
                    "write",
                    path,
                    ui::plain(text::keyed(
                        "shop.log.logfile.write_failed_rolled_back",
                        ss::shop::Tone::Default,
                        {text::lit(path.string())}
                    ))
                );
                return false;
            }
            warnOnce(
                "truncate",
                path,
                ui::plain(text::keyed(
                    "shop.log.logfile.write_and_rollback_failed",
                    ss::shop::Tone::Default,
                    {text::lit(path.string()), text::lit(truncEc.message())}
                ))
            );
            return false;
        }
        warnOnce(
            "write",
            path,
            ui::plain(text::keyed(
                "shop.log.logfile.write_failed",
                ss::shop::Tone::Default,
                {text::lit(path.string())}
            ))
        );
        return false;
    }

    // 写完了。压缩**不在这里做**：那是启动时的一次活（`compressOldLogsOnStartup`），
    // 不该让落一行流水去付 gzip 与目录扫描的账。
    return true;
}

void compressOldLogsOnStartup() {
    auto const* config = currentShopConfig();
    if (config == nullptr || !config->transactionLog.enabled) {
        // 流水关着就什么都不碰：服主关掉日志时不会期望模组再去动他的目录。
        return;
    }
    // 「今天」只能自己算：启动时没有刚写完的文件名可以借。
    auto const today = std::string{isoDateOf(nowIso8601Local())};
    int const  count = compressOldLogs(logDirectory(), today, config->transactionLog.compressAfterDays);
    if (count > 0) {
        Shop::getInstance().getSelf().getLogger().info(ui::msg("shop.log.logfile.compressed_on_startup", count));
    }
}

void resetLogWarnings() { gWarnedFailures.clear(); }

} // namespace ss::shop
