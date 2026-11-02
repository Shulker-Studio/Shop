# 交易流水真机验收清单（P1 票 01 / 02 / 03）

> 三张票里标着「**未验**」的 8 条，全部集中在这里。逐条跑、逐条勾。
> 任何一条没过就**停下来贴日志**，别改代码也别的票面 —— 退路都写在每条下面。

---

## 0. 前置

| 项 | 要求 |
| --- | --- |
| 构建 | `xmake build Shop` 出 `bin/windows/x64/release/Shop`，装进服务器 |
| 权限 | 至少一个 OP 账号 |
| 商店 | ① 一个**普通店**（玩家建、箱里有货、有买入价与卖出价）；② 一个**管理员店**（OP 建，第 1 行是 `config.adminShopName`） |
| 玩家 | 两个：`A`（有钱、要测买卖）、`B`（**故意没钱**，测 `no_money`） |
| 配置 | `plugins/Shop/config/config.json`（**不是** `plugins/Shop/config.json`）。老文件没有 `transactionLog` 块是正常的：加载时按默认 `enabled=true`、`compressAfterDays=7` 补齐并回写 |

**目录布局**（两者是不同层，容易记混）：

```
plugins/Shop/
├── config/config.json                  ← 配置在这里
└── logs/
    ├── transactions/2026-11-02.jsonl   ← 业务流水（一行几十字节）
    └── discarded/2026-11-02.jsonl      ← 销毁留档（一行可能几十 KB）
```

**日志目录**：服务器根目录下 `plugins/Shop/logs/`（不是本机仓库里的），后续命令都进它的两个子目录。
后面所有命令都在这个目录里跑（Git Bash；PowerShell 用 `Get-Content` / `Select-String` 等价替换）。

```bash
cd "<服务器根目录>/plugins/Shop/logs"   # 第一次交易后它才会出现
ls transactions/ discarded/ 2>/dev/null # 新布局：两个子目录
```

先开调试输出，后面每条失败都能靠它定位：

```
/shop debug on
/shop status
```

`/shop status` 里现在应该有这一行（**第 9 条专门验它**）：

```
交易流水：开 compressAfterDays=7（超期压成 .jsonl.gz，永不删除），目录 …/plugins/Shop/logs
```

关掉时是 `交易流水：关（业务流水与 discarded 一起停）`；`compressAfterDays=0` 时括号里是 `（永不压缩）`。

---

## 0.5 升级路径：老配置必须能加载（**本次实测报错的那一条**）

用**升级前那份 `config.json`**（没有 `transactionLog` 块、`version: 1`）直接启动。

- [ ] 模组**加载成功**，控制台没有 `missing required field "transactionLog"`、没有「配置不可用，Shop 拒绝加载」
- [ ] 有一行 `config.json 已补齐并回写（补上默认值：transactionLog）：…`
- [ ] 打开 `plugins/Shop/config/config.json`，已经多出 `transactionLog` 块（`enabled: true`、`compressAfterDays: 7`），**你原来的值一个没变**
- [ ] 再重启一次：不再出现「已补齐」那行（不需要反复补）

顺手验三条宽容规则（可选，但很快）：

- [ ] 手写删掉某一个键（例如 `economy.createObjectiveIfMissing`）→ 能加载，日志说补上了它
- [ ] 把 `transactionLog` 拼成 `transactionlog` → 能加载，日志 WARN「有 1 个不认识的键，已忽略：transactionlog」，实际按默认值开
- [ ] 故意删掉一个逗号 → **拒绝加载**，日志说 JSON 不合法，且**你的文件没有被覆盖**

---

## 0.6 旧文件自动搬家（日志改成按类型分目录）

升级前文件平铺在 `logs/` 根下（`transactions-2026-11-01.jsonl`、`discarded-2026-11-01.jsonl.gz`）。

- [ ] 保留几个旧文件 → 启动 → 它们出现在 `logs/transactions/` 与 `logs/discarded/` 里，**文件名去掉前缀**（`2026-11-01.jsonl`）
- [ ] `logs/` 根下只剩两个子目录（没有残留的 `transactions-*` / `discarded-*`）
- [ ] 控制台有 `已把 N 个旧位置的日志文件搬进 transactions/ 与 discarded/ 子目录`
- [ ] 在 `logs/` 根下放一个自己的文件（`README.md` 或 `我的备份.txt`）→ 重启 → 它**没有被搬走**
- [ ] 再重启一次 → 不再搬（幂等）
- [ ] `discarded/*.jsonl.gz` 搬过来后 `zcat` 仍能读（搬家是 rename，不是重新压缩）

---

## 1. 买一次 → 一行 `ok`（票 01）

**步骤**

1. 玩家 `A` 点普通店的牌子 → 买 1 份 → 确认成交（钱扣了、货到手）。
2. `jq -c . transactions/$(date +%F).jsonl | tail -1`

**期望**

```json
{"ts":"…","event":"trade","result":"ok","dir":"buy","shop":{"dim":0,"x":…,"y":…,"z":…},
 "owner":{"xuid":"…","name":"…"},"admin":false,"backend":"legacy_money",
 "player":{"xuid":"…","name":"A"},"item":{"id":"minecraft:…","aux":0,"name":"…"},
 "units":1,"items":<每份件数>,"unitPrice":<买入价>,"gross":<主交易金额>,"tax":<税>,"total":<玩家实付>}
```

`gross` 与 `total` 的关系（`tax.shopTaxRate` **默认 0**，所以多数服两个数相等）：

| 方向 | `total` | `gross` | `tax` |
| --- | --- | --- | --- |
| 买入 | 玩家实付 = `unitPrice × units` | `total − tax`（税是另一笔付给系统账户的） | 税率 × 成交额 |
| 卖出 | 玩家实收 = `unitPrice × units` | `total`（税由店主另付） | 税率 × 成交额 |

- [ ] 文件出现了，且**恰好一行**（`wc -l` 为 1）
- [ ] `result` 是 `ok`，`dir` 是 `buy`
- [ ] **没有 `reason` 这个键**（`jq 'has("reason")'` 输出 `false`）
- [ ] `units` = 买了几份，`items` = 份数 × 牌面第 2 行的每份件数
- [ ] `unitPrice` = **买入价**（不是卖出价），`total` = `unitPrice × units`，`gross` 与 `tax` 按上面那张表
- [ ] `owner.name` = 牌面第 1 行的店主名，`player.name` = `A` 的名字
- [ ] 中文物品名是原样中文（不是 `\u94bb` 这种）

**没过就这样查**：没有文件 → 看控制台有没有 `日志目录建不出来（…）` / `日志文件打不开（…）`（同进程各只提示一次）；有文件没这行 → `/shop debug on` 后看 `[shop/dbg] trade_result` 的 `outcome` 是不是 `ok`（`outcome` 是 `ok` 却没有行 = 接线断了）；金额不对 → 对 `unitPrice` 取的是哪个方向的价格。

---

## 2. 卖一次 → `dir: "sell"`（票 01）

**步骤**：玩家 `A` 往普通店卖 1 份 → `jq -c . transactions/$(date +%F).jsonl | tail -1`

- [ ] 新增一行，`result` 是 `ok`、`dir` 是 `"sell"`、`player` 是 `A`
- [ ] `unitPrice` = **卖出价**；`gross` 与 `total` 都等于 `unitPrice × units`（卖出方向玩家收到的是牌面额，税由店主另付）

---

## 3. 管理员店卖出 → `admin:true` + 那件货的 NBT（票 01 第 2 条 + 票 02 第 2 条）

**步骤**

1. 玩家 `A` 往**管理员店**卖 1 份（手里带附魔/自定义名的货最好，`nbt` 才有内容可看）。
2. `jq -lc 'select(.result=="ok")' transactions/$(date +%F).jsonl | tail -1`
3. `jq -c . discarded/$(date +%F).jsonl | tail -1`

**期望（业务流水）**

- [ ] `admin` 是 `true`
- [ ] `owner.xuid` 是 `""`（空串，不是创建者的 XUID）
- [ ] `owner.name` = `config.adminShopName` 里的名字（不是牌子上创建者的名字）
- [ ] `tax` 是 `0`，`gross` = `total` = `unitPrice × units`
- [ ] `backend` 是 `legacy_money` / `scoreboard` / `null` 之一（**不是空串**）

**期望（销毁留档）**

- [ ] 一行一件：卖 1 份 64 件就是 1 行；卖 3 份就是 3 行
- [ ] 字段是 `ts` / `shop` / `player` / `item{id,aux,name,nbt}`，`ts` 与上面那条业务流水**同一天**
- [ ] `item.nbt` 是**完整 SNBT**：以 `{` 开头、含 `Count:` / `Name:`（形如 `{Count:1b,Damage:0s,Name:"minecraft:diamond",wasPickedUp:0b}`），附魔/改名/耐久都在里面
- [ ] **不是** `"4 entries"` 这种摘要（那是 `Tag::toString()` 的输出，出现它说明装的还是旧构建）
- [ ] 这些行**不在** `transactions-*.jsonl` 里（NBT 几十 KB，绝不能进业务流水）

**没过就这样查**：`discarded` 缺行而业务流水有行 → `sellUnits` 的 `info.adminShop` 分支没填 `TradeResult::discarded`；两个文件都缺 → 看 warn；`owner.name` 是创建者名 → 改过 `adminShopName` 但没重启。

---

## 4. 余额不足 → `fail` + `no_money`（票 02 第 1 条）

**步骤**（挑一条）

- 简单版：普通店把**买入价**改到远高于 `B` 的余额（管理表单 → 改买入价），玩家 `B` 点牌买 1 份。
- 或：直接把 `B` 的钱清零（经济插件命令 / 记分板 `scoreboard players set`）。

`B` 应该收到「钱不够」的提示，然后：

```bash
jq -c 'select(.result=="fail")' transactions/$(date +%F).jsonl | tail -1
```

- [ ] 有一行 `result` 是 `"fail"`、`reason` 是 `"no_money"`
- [ ] `dir` / `shop` / `player`（`B`）都在；`units` / `items` / `gross` / `tax` / `total` 全是 `0`
- [ ] `admin` / `backend` 与这家店一致（**早失败也要有 `backend`**，不能是 `""`）
- [ ] 钱与货**都没有动**

**没过就这样查**：没这行 → `/shop debug on` 看 `trade_result` 的 `outcome` 是不是 `fail`（是 `fail` 却没行 = `withFacts` 漏在某条返回路径上）；`reason` 是 `over_limit` 而不是 `no_money` → 你的场景先撞上了份数上限，把买入价再调高、货调足。

---

## 5. 同店同人再提交 → `fail` + `frozen_pair`（票 02，只在发生过补偿失败后才有）

**这条不现场造**：`frozen` 需要「钱已动、货取不出、回写也失败」，正常服务器造不出来（票面写的是「难造」）。

- [ ] 事故后核对：`frozen` 行与 `fatal` 日志**成对**出现（同一个 `broken()` 里写）
- [ ] `frozen` 行的 `reason` 是 `compensation_failed`
- [ ] 该「玩家 + 商店」之后的提交记成 `fail` + `frozen_pair`（不是又一条 `frozen`）

对账命令：

```bash
jq -c 'select(.result=="frozen")' transactions-*.jsonl
grep -n "fatal\|compensation" ../../logs/*.log 2>/dev/null | tail -20
```

---

## 6. 8 天前的文件 → 变成 `.gz`，原文件消失（票 03 第 1 条）

判定**只看文件名里的日期，不看 mtime**，所以造素材就是改个名字：

```bash
cd "<服务器根目录>/plugins/Shop/logs/transactions"
src=$(ls *.jsonl | head -1)
cp "$src" "$(date -d '8 days ago' +%F).jsonl"   # 8 天前 → 该压
ls -l
```

2. **交易一笔**（或重启后的第一次写流水）触发扫描 —— 不需要等定时任务，也没有定时任务。

```bash
ls -l
zcat $(date -d '8 days ago' +%F).jsonl.gz | jq -c . | head -2
duckdb -c "select result, count(*) from read_json_auto('*.jsonl*') group by 1;"
```

- [ ] 8 天前那个文件变成 `<日期>.jsonl.gz`，同名 `.jsonl` **不见了**
- [ ] 目录里**没有** `.gz.part` 残留
- [ ] `zcat … | jq -c .` 能逐行读出（内容与压缩前一致）
- [ ] `duckdb` 能同时读到 `.jsonl` 与 `.jsonl.gz`（glob `*.jsonl*`）
- [ ] **今天**的文件与 **7 天内**的文件**没有被压**（`shouldCompress` 是「早于今天 − N 天」，正好 N 天不压）

**没过就这样查**：没被压 → 造文件是不是在「写成功之前」？（触发点在写完日志之后）文件名是不是严格 `YYYY-MM-DD.jsonl`；有 `.gz` 但原文件还在 → 上一轮删原文件失败（有 warn），下次写流水补删；`zcat` 读不出 → 只可能是截断，删掉 `.gz` 让它重压（原文件还在）。

顺带验一次「跨目录轮转」：把一个 8 天前的文件放进 `logs/discarded/`，然后**只做一笔买入**（不卖东西）——`discarded/` 那个文件也应该被压（压缩扫的是日志根目录下的两个子目录，不要求那个目录自己有新写入）。

---

## 7. `compressAfterDays = 0` → 不压（票 03 第 2 条）

1. `plugins/Shop/config/config.json` 里把 `transactionLog` 写成 `{"enabled": true, "compressAfterDays": 0}`。
2. `/ll reload Shop` 或重启服务器（配置只在 `load()` 读一次）。
3. 重新造一个 8 天前的文件 → 交易一笔。

- [ ] `/shop status` 显示 `compressAfterDays=0`（或「永不压缩」）
- [ ] 交易后那个 8 天前的文件**仍然在**，没有 `.gz`
- [ ] 控制台**没有** `日志压缩失败` 之类的告警

---

## 8. `enabled = false` → 两个文件一起停（票 03 第 3 条）

1. `config.json`：`"transactionLog": {"enabled": false, "compressAfterDays": 7}` → `/ll reload Shop` 或重启。
2. 买卖各做一笔（含管理员店卖出——它本来会写 `discarded`）。

```bash
wc -l transactions/*.jsonl discarded/*.jsonl
```

- [ ] 两个文件的行数**都不再增加**（业务流水与 `discarded` 同时停）
- [ ] 交易本身**照常成功**（钱、货、提示都正常，只是不落盘）
- [ ] `/shop status` 显示「交易流水：关（业务流水与 discarded 一起停）」

**测完记得改回** `"enabled": true` 并重启。

---

## 9. `/shop status` 两行（票 03 第 4 条）

- [ ] 有 `交易流水：开 compressAfterDays=7（超期压成 .jsonl.gz，永不删除），目录 …`（关时是「关（业务流水与 discarded 一起停）」）
- [ ] 数字与 `config.json` 一致（尤其老配置缺 `transactionLog` 块时，应显示默认 `7` 而不是 `0` 或空）
- [ ] 游戏内字体下不挤成一行、不串到别的行

---

## 10. 收尾自查（不用造场景，30 秒）

```bash
cd "<服务器根目录>/plugins/Shop/logs/transactions"
jq -e . *.jsonl > /dev/null && echo "每一行都是合法 JSON"
jq -r '.result' *.jsonl | sort | uniq -c        # 只该有 ok / fail
jq -r '.backend' *.jsonl | sort | uniq -c       # 只该有 legacy_money / scoreboard / null
jq -r 'select(.result=="ok") | has("reason")' *.jsonl | sort -u   # 只该有 false
jq -r 'select(.result!="ok") | .reason' *.jsonl | sort | uniq -c  # 全是英文短名
wc -l *.jsonl                                   # 行数
cd ../discarded && jq -e . *.jsonl > /dev/null && echo "留档每一行也是合法 JSON"
```

- [ ] 每一行都是合法 JSON（转义没坏）
- [ ] `result` 只有 `ok` / `fail`（`frozen` 只在事故后出现）
- [ ] `backend` 没有空串
- [ ] `ok` 的行**都没有** `reason` 键；非 `ok` 的行 `reason` 全是英文短名（没有中文）
- [ ] `ts` 是 ISO-8601 带时区偏移（`2026-11-02T03:14:15+08:00`）

---

## 测完要做的事

1. `/shop debug off`。
2. `plugins/Shop/config/config.json` 的 `transactionLog` 改回 `{"enabled": true, "compressAfterDays": 7}`，重启。
3. 删掉造的假文件（`transactions/` 下 `<8 天前>.jsonl` 及其 `.gz`）。
4. 把每条没过的情况贴回来：`jq` 输出 + 控制台 warn/fatal 原文 + `/shop status` 那几行。
