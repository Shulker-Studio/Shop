#include <doctest/doctest.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

/// 词表里的一条：键 → 值。
using Table = nlohmann::json;

Table loadTable(char const* path) {
    std::ifstream in{path};
    REQUIRE_MESSAGE(in.good(), "打不开词表", path);
    Table table;
    in >> table;
    REQUIRE_MESSAGE(table.is_object(), "词表顶层必须是对象", path);
    return table;
}

std::set<std::string> keysOf(Table const& table) {
    std::set<std::string> keys;
    for (auto it = table.begin(); it != table.end(); ++it) {
        keys.emplace(it.key());
    }
    return keys;
}

/// 模板里 `{}` 的个数。漏一个占位符的后果不是编译错误，而是运行时
/// 直接被渲染器判成 `<bad-args:key>`——所以在这里挡住它。
std::size_t slotCount(std::string const& text) {
    std::size_t count = 0;
    for (std::size_t i = 0; i + 1 < text.size(); ++i) {
        if (text[i] == '{' && text[i + 1] == '}') {
            ++count;
            ++i;
        }
    }
    return count;
}

} // namespace

TEST_CASE("词表: zh_CN 与 en_US 的 key 集合必须完全一致") {
    auto const zh = keysOf(loadTable("lang/zh_CN.json"));
    auto const en = keysOf(loadTable("lang/en_US.json"));

    // 少一条 = 那种语言下玩家会看到 key id；多一条 = 有人改了 key 却没清旧条目。
    for (auto const& key : zh) {
        CHECK_MESSAGE(en.count(key) == 1, key);
    }
    for (auto const& key : en) {
        CHECK_MESSAGE(zh.count(key) == 1, key);
    }
}

TEST_CASE("词表: 同一 key 在中英两份里的 {} 占位符个数必须相同") {
    auto const zh = loadTable("lang/zh_CN.json");
    auto const en = loadTable("lang/en_US.json");

    for (auto it = zh.begin(); it != zh.end(); ++it) {
        auto const& key = it.key();
        if (!en.contains(key)) {
            continue; // 上面那条用例已经报过
        }
        auto const& zhText = it.value().get_ref<std::string const&>();
        auto const& enText = en[key].get_ref<std::string const&>();
        CHECK_MESSAGE(slotCount(zhText) == slotCount(enText), key);
    }
}

namespace {

/// 扫一份源码，把 `"shop.xxx.yyy"` 形式的字符串字面量收进来。
///
/// **这不是完整的 C++ 词法分析**，只是抓引号对之间的纯 ASCII `shop.*` 片段：注释里的
/// 同名 key 会被一起收进来（只会多报，不会漏报），而**运行时拼出来的 key**
/// （`"shop.log." + area`）扫不到——那种写法要么改成显式字面量，要么靠人工审查。
/// Shop 目前所有 key 都是整条字面量，这条用例才有意义。
void collectKeys(std::filesystem::path const& file, std::set<std::string>& keys) {
    std::ifstream  in{file};
    std::string    source{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    std::size_t    i = 0;
    while ((i = source.find('"', i)) != std::string::npos) {
        std::size_t const begin = ++i;
        while (i < source.size() && source[i] != '"') {
            if (source[i] == '\\') {
                ++i;
            }
            ++i;
        }
        if (i >= source.size()) {
            break;
        }
        std::string candidate = source.substr(begin, i - begin);
        ++i;
        if (candidate.rfind("shop.", 0) != 0) {
            continue;
        }
        bool ascii = true;
        for (char const c : candidate) {
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.'
                  || c == '_')) {
                ascii = false;
                break;
            }
        }
        if (ascii) {
            keys.emplace(std::move(candidate));
        }
    }
}

std::set<std::string> keysUsedInSources() {
    std::set<std::string> keys;
    std::error_code       ec;
    for (auto const& entry : std::filesystem::recursive_directory_iterator{"src", ec}) {
        if (!entry.is_regular_file()) {
            continue;
        }
        auto const ext = entry.path().extension();
        if (ext == ".cpp" || ext == ".h") {
            collectKeys(entry.path(), keys);
        }
    }
    return keys;
}

/// 把注释与字符串**内容**涂成空格（引号本身保留、总长度不变）。
///
/// 按位置切实参时，注释里的逗号、字符串里的括号都会把括号深度带偏，所以先涂掉。
/// 涂成等长而不是直接删掉，是为了还能拿同一组下标回到原文取字面量。
std::string maskedSource(std::string const& src) {
    std::string out = src;
    std::size_t i   = 0;
    while (i < out.size()) {
        if (src[i] == '/' && i + 1 < out.size() && src[i + 1] == '/') {
            while (i < out.size() && src[i] != '\n') {
                out[i++] = ' ';
            }
        } else if (src[i] == '/' && i + 1 < out.size() && src[i + 1] == '*') {
            out[i++] = ' ';
            out[i++] = ' ';
            while (i < out.size() && !(src[i] == '*' && i + 1 < out.size() && src[i + 1] == '/')) {
                if (src[i] != '\n') {
                    out[i] = ' ';
                }
                ++i;
            }
            for (int k = 0; k < 2 && i < out.size(); ++k) {
                out[i++] = ' ';
            }
        } else if (src[i] == '"') {
            ++i;
            while (i < out.size()) {
                if (src[i] == '\\') {
                    out[i++] = '_';
                    if (i < out.size()) {
                        out[i++] = '_';
                    }
                    continue;
                }
                if (src[i] == '"') {
                    break;
                }
                out[i++] = '_';
            }
            if (i < out.size()) {
                ++i; // 收尾引号本身留着
            }
        } else if (
            src[i] == '\''
            && ((i + 3 < out.size() && src[i + 1] == '\\' && src[i + 3] == '\'')
                || (i + 2 < out.size() && src[i + 2] == '\''))
        ) {
            // 字符字面量（`'{'` / `'\n'`）也涂掉：里面的括号、逗号会把切分带偏。
            // 只认形如 `'x'` / `'\x'` 的三四个字符，`32'768` 这种数字分隔符必须原样留着。
            std::size_t const end = (src[i + 1] == '\\') ? i + 3 : i + 2;
            for (std::size_t k = i + 1; k < end; ++k) {
                out[k] = '_';
            }
            i = end + 1;
        } else {
            ++i;
        }
    }
    return out;
}

using Span = std::pair<std::size_t, std::size_t>;

/// 把 `masked[begin, end)` 按**顶层**逗号切开（括号/花括号/方括号里的逗号不算）。
std::vector<Span> splitTopLevel(std::string const& masked, std::size_t begin, std::size_t end) {
    std::vector<Span> parts;
    int               depth = 0;
    std::size_t       start = begin;
    for (std::size_t i = begin; i < end; ++i) {
        char const c = masked[i];
        if (c == '(' || c == '[' || c == '{') {
            ++depth;
        } else if (c == ')' || c == ']' || c == '}') {
            --depth;
        } else if (c == ',' && depth == 0) {
            parts.emplace_back(start, i);
            start = i + 1;
        }
    }
    // 尾部逗号（`{a, b,}`）会多切出一个空段：那是排版，不是一个实参。
    auto const blank = [&masked](Span const& span) {
        return masked.substr(span.first, span.second - span.first).find_first_not_of(" \t\r\n")
            == std::string::npos;
    };
    if (start < end && !blank(Span{start, end})) {
        parts.emplace_back(start, end);
    }
    return parts;
}

/// `{a, b}` 里的顶层元素个数；不是花括号列表时返回 `nullopt`。
std::optional<std::size_t> elementCount(std::string const& masked, Span const& arg) {
    std::size_t i = arg.first;
    while (i < arg.second && std::isspace(static_cast<unsigned char>(masked[i])) != 0) {
        ++i;
    }
    if (i >= arg.second || masked[i] != '{') {
        return std::nullopt;
    }
    std::size_t close = std::string::npos;
    int         depth = 0;
    for (std::size_t j = i; j < arg.second; ++j) {
        if (masked[j] == '{') {
            ++depth;
        } else if (masked[j] == '}') {
            --depth;
            if (depth == 0) {
                close = j;
                break;
            }
        }
    }
    if (close == std::string::npos) {
        return std::nullopt;
    }
    if (masked.substr(i + 1, close - i - 1).find_first_not_of(" \t\r\n") == std::string::npos) {
        return std::size_t{0};
    }
    return splitTopLevel(masked, i + 1, close).size();
}

/// 一个 `keyed(...)` / `msg(...)` 调用点：第一实参里的 key 字面量 + 实参个数。
struct KeyedCall {
    std::set<std::string> keys;
    std::size_t           argc = 0;
    std::string           where;
};

/// 扫一个源文件里的调用点。
///
/// 与 `collectKeys` 同一档精度：**不是完整的 C++ 词法分析**，认不出的形状（key 是变量、
/// 第三实参不是 `{...}` 字面量……）直接跳过。
/// 之所以值得写：`fmt` 会静默忽略多余实参、缺实参只在渲染时被测试渲染器抓到，而
/// `ShopForm` / `TradeService` 那两层的事件路径没有被表单测试覆盖——这类错只能静态扫。
std::vector<KeyedCall> keyedCalls(std::filesystem::path const& file) {
    std::ifstream in{file};
    std::string   src{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    std::string   masked = maskedSource(src);

    auto const lineOf = [&src](std::size_t pos) {
        return static_cast<std::size_t>(
                   std::count(src.begin(), src.begin() + static_cast<std::ptrdiff_t>(pos), '\n')
               )
             + 1;
    };

    static std::pair<char const*, std::size_t> const kNames[] = {{"keyed", 5}, {"msg", 3}};

    std::vector<KeyedCall> calls;
    for (std::size_t i = 0; i < masked.size(); ++i) {
        for (auto const& [name, length] : kNames) {
            if (masked.compare(i, length, name) != 0) {
                continue;
            }
            // 左边必须是标识符边界：`text::keyed` 的 `:` 算，`xkeyed` 不算。
            if (i > 0 && (std::isalnum(static_cast<unsigned char>(masked[i - 1])) != 0 || masked[i - 1] == '_')) {
                continue;
            }
            std::size_t open = i + length;
            while (open < masked.size() && std::isspace(static_cast<unsigned char>(masked[open])) != 0) {
                ++open;
            }
            if (open >= masked.size() || masked[open] != '(') {
                continue;
            }

            int         depth = 0;
            std::size_t close = std::string::npos;
            for (std::size_t j = open; j < masked.size(); ++j) {
                if (masked[j] == '(') {
                    ++depth;
                } else if (masked[j] == ')') {
                    --depth;
                    if (depth == 0) {
                        close = j;
                        break;
                    }
                }
            }
            if (close == std::string::npos) {
                continue;
            }

            auto const args = splitTopLevel(masked, open + 1, close);
            if (args.empty()) {
                continue;
            }

            // 第一实参里的每个 `shop.*` 字面量都收进来：三元（`cond ? "a" : "b"`）两支
            // 走同一个调用点，实参个数自然相同。
            std::set<std::string> keys;
            for (std::size_t q = args.front().first; q < args.front().second; ++q) {
                if (masked[q] != '"') {
                    continue;
                }
                std::size_t const quoteEnd = masked.find('"', q + 1);
                if (quoteEnd == std::string::npos || quoteEnd >= args.front().second) {
                    break;
                }
                std::string const literal = src.substr(q + 1, quoteEnd - q - 1);
                if (literal.rfind("shop.", 0) == 0) {
                    keys.emplace(literal);
                }
                q = quoteEnd;
            }
            if (keys.empty()) {
                continue; // key 是变量或拼出来的，认不出
            }

            std::optional<std::size_t> argc;
            if (std::string_view{name} == "msg") {
                argc = args.size() - 1;
            } else if (args.size() <= 2) {
                argc = std::size_t{0}; // keyed(key) / keyed(key, tone)
            } else {
                argc = elementCount(masked, args.back());
            }
            if (!argc) {
                continue;
            }

            calls.push_back(KeyedCall{std::move(keys), *argc, file.string() + ":" + std::to_string(lineOf(i))});
        }
    }
    return calls;
}

} // namespace

/// 这条用例是给「加了一句新文案但忘了写词表」兜底的：`ui::msg(key)` 查不到会
/// 原样输出 key id，而那条路径（日志、命令输出）没有被严格的测试渲染器覆盖。
TEST_CASE("词表: 源码里引用的每个 shop.* key 都必须在 zh_CN.json 里") {
    auto const used = keysUsedInSources();
    auto const zh   = keysOf(loadTable("lang/zh_CN.json"));

    REQUIRE_MESSAGE(!used.empty(), "一个 key 都没扫到，说明扫描路径或 rundir 不对");
    for (auto const& key : used) {
        CHECK_MESSAGE(zh.count(key) == 1, key);
    }
}

/// 反方向：词表里留着没人用的 key 就是**改名的残留**（新名加上了、旧名忘了删）。
/// 这类残留不会让玩家看到错字，但会让下一个人以为那条还在用，改文案时改错地方。
TEST_CASE("词表: 每条 key 都必须被源码用到（不许留改名残留）") {
    auto const used = keysUsedInSources();
    auto const zh   = loadTable("lang/zh_CN.json");

    REQUIRE_MESSAGE(!used.empty(), "一个 key 都没扫到，说明扫描路径或 rundir 不对");
    for (auto it = zh.begin(); it != zh.end(); ++it) {
        CHECK_MESSAGE(used.count(it.key()) == 1, it.key());
    }
}

/// 调用点的实参个数必须等于词表里的 `{}` 个数。
///
/// 两个方向都会坏：**少了**实参时 `fmt::vformat` 抛异常（`mod/Text.cpp` 兜住后原样输出模板，
/// 玩家看到一屏带 `{}` 的半成品）；**多了**实参时 fmt 静默忽略——而多出来的那个参数
/// 往往就是这句文案本该显示的数字（`确认买入（{} 份）` 就是这么丢的）。
TEST_CASE("词表: keyed()/msg() 的实参个数必须等于词表的占位符个数") {
    auto const zh = loadTable("lang/zh_CN.json");

    std::size_t     checked = 0;
    std::error_code ec;
    for (auto const& entry : std::filesystem::recursive_directory_iterator{"src", ec}) {
        if (!entry.is_regular_file()) {
            continue;
        }
        auto const ext = entry.path().extension();
        if (ext != ".cpp" && ext != ".h") {
            continue;
        }
        for (auto const& call : keyedCalls(entry.path())) {
            for (auto const& key : call.keys) {
                if (!zh.contains(key)) {
                    continue; // 上面那条用例已经报过「词表缺 key」
                }
                auto const& tmpl = zh[key].get_ref<std::string const&>();
                CHECK_MESSAGE(slotCount(tmpl) == call.argc, key, " @ ", call.where);
                ++checked;
            }
        }
    }

    // 扫描器自己不能瞎：一个调用点都没认出时，这条用例等于没跑。
    REQUIRE_MESSAGE(checked > 100, "只认出了 ", checked, " 个调用点，扫描器多半坏了");
}
