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

> **启用前必须把 LegacyMoney 配置里的 `pay_tax` 设为 `0`。** 本模组读不到那个值（LegacyMoney 没有导出它的
> getter），只能启动时打一条 WARN 提醒；若 `pay_tax` 仍大于 0，每笔转账的收款方都会被再扣一次税，
> 店主会被抽两次，退款补偿也会与实际到账不一致。
>
> 商店税需要后端支持「单边扣钱」（从买家扣掉、谁都不加 = 销毁）。`legacy_money` 与 `scoreboard`
> 都支持：前者是 `LLMoney_Trans(from, "", n)`，后者是只 `reduce` 买家。将来出现不支持的后端时，
> `tax.shopTaxRate` 在那个后端上会整笔不生效，启动日志会写明这一条。

### 交易流水（JSONL，P1）

每笔**终态**交易自己留一行，位置 `plugins/Shop/logs/transactions-YYYY-MM-DD.jsonl`（按天分文件，服务器本地时间）。
一行一个 JSON 对象（NDJSON）、键序固定、UTF-8 无 BOM、中文不转义，`jq` / `duckdb` 直接读。

`result` 只有三种：`ok`（成交）、`fail`（拒绝或没成交，带机器可读的 `reason`，如 `no_money` / `no_stock` /
`no_space` / `owner_offline` / `shop_closed` / `frozen_pair`）、`frozen`（补偿失败被冻结，
`reason` 为 `compensation_failed`，同时有一条 `fatal` 日志）。等待玩家确认的那一屏**不记**——它不是终态。

管理员店卖出时被**销毁**的货另写一个文件 `discarded-YYYY-MM-DD.jsonl`：一行一件物品堆，
带完整 SNBT（`item.nbt`），所以它可能与业务流水不是同一个量级。两个文件同一套轮转规则。

**永不删除**：超过 `transactionLog.compressAfterDays` 天（默认 7）的文件压成 `.jsonl.gz`，
压缩成功后才删原文件。`0` 表示永不压缩；负数被拒（配置非法不加载）。要清盘请服主自己 `rm`。
`transactionLog.enabled = false` 时**两个文件一起停**（不存在“只关一个”的组合），交易照常进行。

```bash
# 1. 今天卖了什么：只看成交，挑几个字段
jq 'select(.result=="ok") | {ts, dir, item: .item.name, units, total}' plugins/Shop/logs/transactions-2026-11-01.jsonl

# 2. 查已压缩的历史（.jsonl.gz 直接 zcat 进 jq）
zcat plugins/Shop/logs/transactions-2026-10-2*.jsonl.gz | jq -c 'select(.player.xuid=="2535400000000002")'

# 3. 用 duckdb 聚合（.jsonl 与 .jsonl.gz 都能直读）
duckdb -c "SELECT owner.name, sum(total) AS 营业额 FROM read_json_auto('plugins/Shop/logs/transactions-*.jsonl*') WHERE result='ok' GROUP BY 1 ORDER BY 2 DESC"
```

### 配置项（P0 已生效）

`adminShopName`、`shopContainers`、`maxShopAmount`、`allowPartialTransactions`、`blockSellPriceHigherThanBuy`、
`allowOwnerOpenChest`、`shopInteractionCooldownMs`、`ignoreCreativeMode`、`economy.backend`（`legacy_money` /
`scoreboard` / `null`；`auto` 已删除）、`economy.scoreboardObjective`、`economy.createObjectiveIfMissing`、
`economy.requireOwnerOnline`、`economy.currencySymbol`、`tax.shopTaxRate`、
`transactionLog.enabled`（默认 `true`）、`transactionLog.compressAfterDays`（默认 `7`，`0` = 永不压缩）。
（`tax.taxDestination` 等仍留待 P1，见 `DESIGN.md` 第 15 节。）

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
