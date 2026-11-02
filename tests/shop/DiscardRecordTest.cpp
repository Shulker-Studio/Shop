#include "shop/DiscardRecord.h"

#include <doctest/doctest.h>
#include <string>

namespace {

using namespace ss::shop;

DiscardRecord diamond() {
    DiscardRecord record;
    record.ts          = "2026-11-01T13:23:12+08:00";
    record.dimensionId = 0;
    record.x           = 100;
    record.y           = 64;
    record.z           = -7;
    record.playerXuid  = "2535400000000001";
    record.playerName  = "Alex";
    record.item        = {"minecraft:diamond", 0, "钻石", R"({Count:64b,Name:"minecraft:diamond"})"};
    return record;
}

} // namespace

TEST_CASE("销毁留档: 一行一个对象，键序固定 (jq 直读)") {
    // 键序是本票对外契约的一部分： `ts` / `shop{dim,x,y,z}` / `player{xuid,name}` / `item{id,aux,name,nbt}`
    CHECK(
        discardRecordJson(diamond())
        == R"({"ts":"2026-11-01T13:23:12+08:00","shop":{"dim":0,"x":100,"y":64,"z":-7},"player":{"xuid":"2535400000000001","name":"Alex"},"item":{"id":"minecraft:diamond","aux":0,"name":"钻石","nbt":"{Count:64b,Name:\"minecraft:diamond\"}"}})"
               "\n"
    );
}

TEST_CASE ("销毁留档： 一行就是一个对象，坏字符不会把记录撕成两行") {
    auto record = diamond ();
    record.item.name = "带\"引号\"和\\反斜杠的物品";
    record.item.nbt  = "{Name:\"a\\b\",Lore:[\"第一行\n第二行\"]}";

    auto const json = discardRecordJson(record);

    // 恰好一个换行，而且在末尾： 整条记录必须是一行（NDJSON 的硬要求）
    CHECK(json.find('\n') == json.size() - 1);
    CHECK(json.find("\\\"引号\\\"") != std::string::npos);
    CHECK(json.find("\\\\反斜杠") != std::string::npos);
    // nbt 里的换行必须被转义，否则一条记录会变成两行、第二行还不是合法 JSON
    CHECK(json.find("\\n") != std::string::npos);
    CHECK(json.find('\r') == std::string::npos);
}

TEST_CASE("销毁留档: 中文原样写 (UTF-8 不转成 \\u)") {
    auto const json = discardRecordJson(diamond());
    CHECK(json.find("钻石") != std::string::npos);
    CHECK(json.find("\\u94bb") == std::string::npos);
}

TEST_CASE("销毁留档: nbt 原样透传，不截断也不改写") {
    auto       record = diamond();
    std::string const nbt(4096, 'x'); // 完整 SNBT 可能很长，必须整条留着
    record.item.nbt = nbt;

    CHECK(discardRecordJson(record).find(nbt) != std::string::npos);
}

TEST_CASE("销毁留档: 文件名按日期生成") {
    // 类型由目录（`logs/discarded/`）区分，文件名只留日期——与业务流水同一套命名。
    CHECK(discardLogFileName("2026-11-01") == "2026-11-01.jsonl");
    // 日期直接取 ts 的前 10 个字符，所以两者永远同一天
    CHECK(discardLogFileName(diamond().ts.substr(0, 10)) == "2026-11-01.jsonl");
}
