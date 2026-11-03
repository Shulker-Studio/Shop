# Shop

基岩版（LeviLamina）告示牌箱子商店，复刻 Java 版 [ChestShop-3](https://github.com/ChestShop-authors/ChestShop-3) 的核心玩法。

> 当前状态：P0 功能已落地（开店、买卖、牌面/容器/破坏/内容四层保护、经济后端与商店税），
> 真机验收清单见 [DESIGN.md](./DESIGN.md) 第 13 节。

## 玩法（目标形态）

在箱子旁边放一块告示牌，写上四行：

```
Steve
64
B 10 : S 5
?
```

| 行 | 内容 |
| --- | --- |
| 1 | 店主名（OP 写 `Admin Shop` 为管理员商店） |
| 2 | 每次交易数量 |
| 3 | 买入价 / 卖出价，`B` 和 `S` 至少写一个 |
| 4 | 商品，写 `?` 由服务器从箱子里自动填 |

点击告示牌弹出表单，选买卖和数量。非店主不能开箱、不能拆牌、不能拆箱。

### 管理员商店（P0）

OP 把第 1 行写成 `adminShopName`（默认 `Admin Shop`）就是管理员商店；非 OP 写同一个名字只是普通商店（店主是自己）。

- 库存同样来自容器（必须挂着箱子），卖光了就是真的没货。
- 资金侧是「系统账户」：玩家买入时钱被销毁，卖出时钱凭空记到玩家账上——没有中间人收钱，也没有余额上限。
- 所有人（包括 OP）都能在管理员店买卖；管理（改牌、开箱、拆箱）只给 OP。
- 不抽商店税；不看店主是否在线。后端当不了系统账户时，点牌会直接写明原因且不给买卖按钮。

## 依赖

| 依赖 | 版本 | 说明 |
| --- | --- | --- |
| [LeviLamina](https://github.com/LiteLDev/LeviLamina) | 兼容 `>= 26.51.7 < 26.60.0`，构建基线 `26.51.8` | 模组加载器 |
| [iListenAttentively](https://github.com/LiteLDev/iListenAttentively) | 兼容 `>= 0.17.0`，构建基线 `0.17.0` | 事件层（告示牌编辑、开箱、爆炸/活塞/凋灵/火焰、双箱、货币事件） |
| [LegacyMoney](https://github.com/LiteLDev/LegacyMoney) | 可选 | 经济后端；装了 [LegacyScriptEngine](https://github.com/LiteLDev/LegacyScriptEngine) 的服务器会自带 |

**iListenAttentively 是硬依赖**：Shop 链接它的 DLL 导入表，未安装时 BDS 会直接加载失败，不是"功能缺失"。

### 经济后端

**必须明确选一个，不会自动切换**（`economy.backend`，默认 `legacy_money`），见 `DESIGN.md` 第 8 节。

| 取值 | 用的是什么 | 要点 |
| --- | --- | --- |
| `legacy_money`（默认） | `LegacyMoney.dll`（装 LegacyScriptEngine 的服务器自带） | 支持离线店主，每笔交易一次原子转账，能收商店税 |
| `scoreboard` | 玩家在 `economy.scoreboardObjective`（默认 `money`）上的分值 | 支持离线店主（玩家在本服留过分值即可），金额上限 `int32`，能收商店税 |
| `null` | 空实现 | 余额恒为 0、交易全失败，只给开发用 |

写 `scoreboard` 时目标默认由模组自动建（`economy.createObjectiveIfMissing` 默认 `true`，用 `dummy` 判据）；
想自己建就先 `/scoreboard objectives add money dummy` 再把那个开关设为 `false`。

选中的后端用 `/shop status` 或启动日志看。**探不到就报错并不启用交易**（余额读不到，表单里不编造数字），
提示会说明改哪个值能解决——本模组不会因为另一个后端恰好可用就悄悄换一套账本（余额会一夜之间全变 0）。
老配置里写 `auto` 会被拒绝加载，请改成上面三个值之一。

### 商店税（可选，默认关闭）

`tax.shopTaxRate`（百分比，`0.05` = 5%）由**店主承担**：买家总支出不变，店主实收少一笔，差额被销毁。

税率只支持**千分位**（`0.001` = 0.1%）。写成 `0.0005`、或落到 `[0, 1)` 之外时**不会拒绝加载**：
启动日志 WARN 一次，本次运行**按 `0` 处理**（这个配置失效，不收商店税），交易与全部保护照常。
所以看到税率不对，改完 `config.json` 用 `/shop reload` 重读即可，不用重启。

税额按成交总额**向下取整**，**但不足 1 时按 1 收**。没有这个下限，把一笔大单拆成若干小单
就能把税拆没（拆单对买卖双方都有利可图）。代价是极小额的交易：`1` 元的交易税 `1` 元，店主实收 `0`。

> **只要 `economy.backend` 是 `legacy_money`，就必须把 LegacyMoney 配置里的 `pay_tax` 设为 `0`。**
> 本模组读不到那个值（LegacyMoney 没有导出它的 getter），只能在启动时无条件打一条 WARN。
> 若 `pay_tax` 仍大于 0，每笔转账的收款方都会被再扣一次税：不仅商店税收不准，
> **补偿退款也会让一方固定亏一截**（退款按原金额反向转，但到账少了那一笔）——
> 所以这个提醒与 `tax.shopTaxRate` 开不开无关。
>
> 商店税需要后端支持「单边扣钱」（从买家扣掉、谁都不加 = 销毁）。`legacy_money` 与 `scoreboard`
> 都支持：前者是 `LLMoney_Trans(from, "", n)`，后者是只 `reduce` 买家。将来出现不支持的后端时，
> `tax.shopTaxRate` 在那个后端上会整笔不生效，启动日志会写明这一条。

### 语言与消息外观

所有玩家可见文本都走同一张词表：

```
lang/zh_CN.json   中文（值是模板，例如 "shop.form.units_reduced.buy": "这家店现在只够成交 {} 份"）
lang/en_US.json   英文
```

- 文件名就是 locale code，放在 `plugins/Shop/lang/`（安装包里已带）。
- 模板里的 `{}` 由代码按顺序填入（价格、份数、玩家名），所以**数字在句子里的位置由译文决定**，
  不需要为了语序去改代码。
- **`lang/` 是必需的，不是可选装饰**：代码里没有中文原文，缺 `lang/` 时玩家看到的是
  `shop.form.button.buy` 这类 key id（控制台会 WARN 提示重装）。升级时别只换 dll。
- 改完语言文件要 `/ll reload Shop` 或重启：`/shop reload` 只重读 `config.json`，
  **不碰 `lang/`**（`ll::i18n` 的重复载入语义没有文档保证，不做未经实机验证的热重载）。
- 某条模板少一个 `{}`、或响应里插了非法占位符：那条显示成 `<bad-args:key>`，其余照常。
- 所有玩家文本（表单、按钮、拒绝原因、交易结果）都走同一张词表，按**玩家自己的语言**渲染，不是服务器统一一种。
- 后台日志与控制台的 `/shop` 输出也走同一张词表（按服务器默认语言），所以改词表不会出现
  「聊天里是新说法、日志里还是旧的」。日志会**去掉颜色码**——`§8` 混进日志只会污染检索。
  流水 JSON 里的字段名与事件名不翻译：那是给 `rg` 和告警规则看的协议。
- 聊天消息统一是 `§8[§6Shop§8]§r` + 语义色：成功绿、失败红、警告黄、提示灰。
- 表单正文：标签灰、值白、标题加粗金；买卖按钮绿/红、管理黄、取消深灰。

加一种语言：复制 `en_US.json` 改名成目标 locale，逐条翻译（保留 `{}` 的数量与顺序），
重启或 `/ll reload Shop`。

### 交易流水（JSONL，P1）

每笔**终态**交易自己留一行，位置 `plugins/Shop/logs/transactions/YYYY-MM-DD.jsonl`（按天分文件，服务器本地时间）。
一行一个 JSON 对象（NDJSON）、键序固定、UTF-8 无 BOM、中文不转义，`jq` / `duckdb` 直接读。

`result` 只有三种：`ok`（成交）、`fail`（拒绝或没成交，带机器可读的 `reason`，如 `no_money` / `no_stock` /
`no_space` / `owner_offline` / `shop_closed` / `frozen_pair`）、`frozen`（补偿失败被冻结，
`reason` 为 `compensation_failed`，同时有一条 `fatal` 日志）。等待玩家确认的那一屏**不记**——它不是终态。

管理员店卖出时被**销毁**的货另写一个目录 `plugins/Shop/logs/discarded/YYYY-MM-DD.jsonl`：一行一件物品堆，
`item.nbt` 是完整 SNBT（紧凑单行，如 `{Count:1b,Damage:0s,Name:"minecraft:diamond",wasPickedUp:0b}`）。
两类记录**分目录**放，因为它们体量差两个数量级（业务流水一行几十字节，留档一行可能几十 KB）：

```
plugins/Shop/logs/
├── transactions/2026-11-01.jsonl       ← 业务流水
├── transactions/2026-10-25.jsonl.gz    ← 超期压缩后
├── discarded/2026-11-01.jsonl          ← 销毁留档（NBT 很胖）
└── discarded/2026-10-25.jsonl.gz
```

两个目录同一套轮转规则；压缩**只扫这两个子目录**，`logs/` 根下你自己放的文件（备份、旧布局残留）
不会被碰。旧版本留下的文件平铺在 `logs/` 根下（带 `transactions-` / `discarded-` 前缀），
模组启用时会**自动搬进**对应子目录（同盘 rename，不覆盖任何已有文件，失败只留一条 warn）。

压缩**只在模组启用（服务器启动 / `/ll reload Shop`）时做一次**，不在成交路径上：早先是每写一行
流水就扫一遍目录、必要时当场 gzip 一整个旧文件，那笔卡顿落在玩家刚买完东西那一拍的 tick 上。
启动时同步压完（服主本来就在等），压了几个文件会打一条 info；压失败的原文件留给下次启动重试。

**永不删除**：超过 `transactionLog.compressAfterDays` 天（默认 7）的文件压成 `.jsonl.gz`，
压缩成功后才删原文件。`0` 表示永不压缩；负数被拒（配置非法不加载）。要清盘请服主自己 `rm`。
`transactionLog.enabled = false` 时**两个文件一起停**（不存在“只关一个”的组合），交易照常进行。

```bash
# 1. 今天卖了什么：只看成交，挑几个字段
jq 'select(.result=="ok") | {ts, dir, item: .item.name, units, total}' plugins/Shop/logs/transactions/2026-11-01.jsonl

# 2. 查已压缩的历史（.jsonl.gz 直接 zcat 进 jq）
zcat plugins/Shop/logs/transactions/2026-10-2*.jsonl.gz | jq -c 'select(.player.xuid=="2535400000000002")'

# 3. 用 duckdb 聚合（.jsonl 与 .jsonl.gz 都能直读）
duckdb -c "SELECT owner.name, sum(total) AS 营业额 FROM read_json_auto('plugins/Shop/logs/transactions/*.jsonl*') WHERE result='ok' GROUP BY 1 ORDER BY 2 DESC"
```

### 配置项（P0 已生效）

`adminShopName`、`shopContainers`、`bannedItems`（禁售物品：正则，整串匹配物品 ID 如
`minecraft:bedrock`；装进潜影盒里嵌套也算；只管玩家开的店，管理员店不受影响。
收纳袋一律不能当商品——内容存在服务端的动态容器里读不出来，与禁售表无关）、
`maxShopAmount`、`allowPartialTransactions`、`blockSellPriceHigherThanBuy`、
`allowOwnerOpenChest`、`shopInteractionCooldownMs`、`ignoreCreativeMode`、`economy.backend`（`legacy_money` /
`scoreboard` / `null`；`auto` 已删除）、`economy.scoreboardObjective`、`economy.createObjectiveIfMissing`、
`economy.requireOwnerOnline`、`economy.currencySymbol`、`tax.shopTaxRate`、
`transactionLog.enabled`（默认 `true`）、`transactionLog.compressAfterDays`（默认 `7`，`0` = 永不压缩）。
（`tax.taxDestination` 等仍留待 P1，见 `DESIGN.md` 第 15 节。）

## 命令

- `/shop status`：版本、当前配置、hooks 状态、经济后端与流水目录。
- `/shop debug <on|off>`：打开后每次牌面判定 / 保护拦截 / 交易结果都会打一行 `[shop/dbg]`。
- `/shop reload`：**只重读 `config.json`**，不卸载模组、不重挂 hook。改价格上限、税率、各种开关时用它；
  新配置解析失败会保留旧配置并在控制台说明原因。`/ll reload Shop` 仍然可用，但它会重建监听器与经济后端。

### 配置文件与补齐规则

配置文件在 `plugins/Shop/config/config.json`（日志在 `plugins/Shop/logs/transactions/` 与
`plugins/Shop/logs/discarded/`，是**不同**的两层目录）。

**缺键不会让模组拒绝加载。** 加载时永远拿本版的默认值打底、用文件里的值覆盖，所以：

- 升级后新增的配置块（例如 `transactionLog`）在旧文件里没有 → 按默认值生效，启动日志会写明
  `config.json 已补齐并回写（补上默认值：transactionLog）`，文件随即被补成完整的一份。
- 手写配置漏了一个块或一个键 → 同上，按默认值跑；不需要为了加一个开关把整个文件重写一遍。
- 键名拼错（`transactionlog`、`scoreboardObjectiv`）→ 那个键**不生效**，启动时 WARN 列出被忽略的键。
- 文件不是合法 JSON（少了逗号、多了注释外的括号）→ **拒绝加载且不覆盖你的文件**，
  日志里给出出错位置；修好再启动。
- 值非法（`maxShopAmount: 0`、`compressAfterDays: -1` …）→ 拒绝加载，
  日志写明哪一项不合法——这跟缺键是两回事，缺键是「没写」，写错是「写了但不合理」。
- **例外是 `tax.shopTaxRate`**：它越界或精度超出千分位时不拒绝加载，只 WARN 并把本次运行的税率按 `0`。
  一个税率数字写错不该让交易与全部保护一起关掉，代价是不收税而不是模组不工作。

## 构建

```bash
xmake f -a x64 -m release -p windows -y
xmake -y          # 构建 Shop.dll
xmake test        # 构建并运行纯逻辑层单元测试（doctest）
```

产物在 `bin/Shop/`。

`xmake test` 只编译 `src/shop/` 与 `tests/`，不链接 LeviLamina，所以没有 BDS 也能跑；解析器与计价模块不允许 `#include` 任何 `mc/` 头，这条在 `xmake` 与 `xmake test` 两种构建里都会断言。

需要：

- [xmake](https://xmake.io)
- Visual Studio（勾选"使用 C++ 的桌面开发"）
- LLVM（clang-cl）

首次构建会拉取 LeviLamina 与 iListenAttentively 的源码包，网络受限时配置 xmake 代理。

## 安装

```bash
lip install github.com/lwenk/Shop
```

或手动把 `bin/Shop/` 放到服务器的 `plugins/Shop/`。

## 文档

- [DESIGN.md](./DESIGN.md)：完整设计（数据模型、事件与 hook、交易流程、保护规则、经济契约、决策记录）
- [CHANGELOG.md](./CHANGELOG.md)

## 许可证

AGPL-3.0，见 [LICENSE](./LICENSE)。
