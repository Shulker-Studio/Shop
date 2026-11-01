# 01: 每笔成交落一行（模型 + 写文件 + 接漏斗）

**What to build:** 买一次东西，`plugins/Shop/logs/transactions-YYYY-MM-DD.jsonl` 里就多一行 `result: "ok"` 的记录，`jq .` 能解析，字段一眼能看出「谁、在哪家店、买了什么、几份、多少钱」。

**Blocked by:** None (can start immediately)

**Status:** ready-for-agent

- [ ] 纯逻辑 `TradeRecord` 结构 + 序列化：**一行一个 JSON 对象**、键序固定、以换行符结尾、UTF-8 无 BOM、中文不转义；用 `nlohmann::ordered_json` 而不是拼字符串
- [ ] 字段按 spec 表格：`ts` / `event` / `result` / `reason` / `dir` / `shop{dim,x,y,z}` / `owner{xuid,name}` / `admin` / `backend` / `player{xuid,name}` / `item{id,aux,name}` / `units` / `items` / `unitPrice` / `gross` / `tax` / `total`
- [ ] 组合校验：`ok` 不带 `reason`；`fail`/`frozen` 必须带；`admin == true` 时 `tax == 0` 且 `owner.xuid` 为空串
- [ ] 依赖方向：`reason` 复用 `src/shop` 的 `TradeRejection`，**不许**引用 `src/mod` 的 `TradeStatus`；`Done/Rejected/Broken/NeedsConfirm` 到流水三态的映射写在 mod 一侧
- [ ] `ts` 由调用方传入（不在纯逻辑里读时钟）；文件名规则是纯函数（给 `ts` 出 `transactions-YYYY-MM-DD.jsonl`）
- [ ] 写文件：目录 `plugins/Shop/logs/` 不存在就建；每次 `append` → 写一行 → `flush` → 关；跨天自动换文件（不缓存"当前文件"）
- [ ] 写失败（磁盘满/权限）只 `warn`（**同进程一次**），交易照常成功
- [ ] 接线点**只有** `finishTrade`（两个方向都经过它，且拿得到 `result.plan`）；本票只处理 `Done → ok`，`Rejected`/`Broken` 留到 02
- [ ] 单测：三种记录的快照字符串（锁键序与转义）、含引号与换行的自定义物品名仍是一行、文件名、矛盾组合被拒
- [ ] 真机：买一次 → `jq . transactions-*.jsonl` 看到一行 `ok`，金额与件数与牌面对得上；再卖一次看到 `dir: "sell"`
- [ ] `xmake build Shop` / `xmake test` 全绿，测试数与断言数写进本票

## 备注

- **`nlohmann_json` 现在不是本项目的依赖**（`levilamina` 包里也不带头文件）。先在 `xmake.lua` 加
  `add_requires("nlohmann_json")` 与 `add_packages("nlohmann_json")`（本机缓存已有 v3.12.0），别假设它现成可用。
- 这是**数据**不是日志：不要写进 LeviLamina 的 logger，也不要依赖它的级别。路径用 `std::filesystem` 拼。
- spec 只要求 P1 记终态：`NeedsConfirm` 不是终态，别写。

## Answer

（待实现）
