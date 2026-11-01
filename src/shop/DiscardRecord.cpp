#include "shop/DiscardRecord.h"

#include <cstdio>

namespace ss::shop {
namespace {

/// 把一个字符串写成 JSON 字面量。
///
/// 只转义 JSON 要求的那几个字符（引号、反斜杠、控制字符），UTF-8 的多字节序列**原样写**——
/// 中文名写成 `\u94bb\u77f3` 之后 `jq` 还是那个字，但人眼在文件里就找不到了。
void appendJsonString(std::string& out, std::string_view value) {
    out.push_back('"');
    for (char const ch : value) {
        switch (ch) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\b':
            out += "\\b";
            break;
        case '\f':
            out += "\\f";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (static_cast<unsigned char>(ch) < 0x20) {
                // 其余控制字符没有短转义，写 \u00XX（SNBT 里偶尔会夹带）
                char buf[8];
                std::snprintf(buf, sizeof buf, "\\u%04x", static_cast<unsigned>(static_cast<unsigned char>(ch)));
                out += buf;
                break;
            }
            out.push_back(ch);
            break;
        }
    }
    out.push_back('"');
}

void appendKey(std::string& out, std::string_view key) {
    out.push_back('"');
    out.append(key);
    out += "\":";
}

} // namespace

std::string discardRecordJson(DiscardRecord const& record) {
    std::string json;
    json.reserve(256 + record.item.nbt.size());

    json.push_back('{');
    appendKey(json, "ts");
    appendJsonString(json, record.ts);

    json.push_back(',');
    appendKey(json, "shop");
    json.push_back('{');
    appendKey(json, "dim");
    json += std::to_string(record.dimensionId);
    json.push_back(',');
    appendKey(json, "x");
    json += std::to_string(record.x);
    json.push_back(',');
    appendKey(json, "y");
    json += std::to_string(record.y);
    json.push_back(',');
    appendKey(json, "z");
    json += std::to_string(record.z);
    json.push_back('}');

    json.push_back(',');
    appendKey(json, "player");
    json.push_back('{');
    appendKey(json, "xuid");
    appendJsonString(json, record.playerXuid);
    json.push_back(',');
    appendKey(json, "name");
    appendJsonString(json, record.playerName);
    json.push_back('}');

    json.push_back(',');
    appendKey(json, "item");
    json.push_back('{');
    appendKey(json, "id");
    appendJsonString(json, record.item.id);
    json.push_back(',');
    appendKey(json, "aux");
    json += std::to_string(record.item.aux);
    json.push_back(',');
    appendKey(json, "name");
    appendJsonString(json, record.item.name);
    json.push_back(',');
    appendKey(json, "nbt");
    appendJsonString(json, record.item.nbt);
    json.push_back('}');

    json += "}\n";
    return json;
}

std::string discardLogFileName(std::string_view date) {
    return "discarded-" + std::string{date} + ".jsonl";
}

} // namespace ss::shop
