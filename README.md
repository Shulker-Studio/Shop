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
| 1 | 店主名（写 `Admin Shop` 为管理员商店） |
| 2 | 每次交易数量 |
| 3 | 买入价 / 卖出价，`B` 和 `S` 至少写一个 |
| 4 | 商品，写 `?` 由服务器从箱子里自动填 |

点击告示牌弹出表单，选买卖和数量。非店主不能开箱、不能拆牌、不能拆箱。

## 依赖

| 依赖 | 版本 | 说明 |
| --- | --- | --- |
| [LeviLamina](https://github.com/LiteLDev/LeviLamina) | 兼容 `>= 26.51.7 < 26.60.0`，构建基线 `26.51.8` | 模组加载器 |
| [iListenAttentively](https://github.com/LiteLDev/iListenAttentively) | 兼容 `>= 0.17.0`，构建基线 `0.17.0` | 事件层（告示牌编辑、开箱、爆炸/活塞/凋灵/火焰、双箱、货币事件） |
| [LegacyMoney](https://github.com/LiteLDev/LegacyMoney) | 可选 | 经济后端；装了 [LegacyScriptEngine](https://github.com/LiteLDev/LegacyScriptEngine) 的服务器会自带 |

**iListenAttentively 是硬依赖**：Shop 链接它的 DLL 导入表，未安装时 BDS 会直接加载失败，不是"功能缺失"。

### 经济后端

按 `LegacyMoney → 记分板 → 空实现` 的顺序自动探测，最终选择了哪个会写进启动日志，见 `DESIGN.md` 第 8 节。

- 装了 `LegacyMoney.dll`（装了 LegacyScriptEngine 的服务器会自带）就用它：支持离线店主，每笔交易一次原子转账。
- 否则用记分板：余额是玩家在 `economy.scoreboardObjective`（默认 `money`）上的分值，**只支持在线店主**。
  目标不存在时默认不会自动创建；想让模组自己建就把 `economy.createObjectiveIfMissing` 设为 `true`。
- 两家都没有时会明确报「没有可用的经济后端」，交易按钮不出现。

### 商店税（可选，默认关闭）

`tax.shopTaxRate`（百分比，`0.05` = 5%）由**店主承担**：买家总支出不变，店主实收少一笔，差额被销毁。

> **启用前必须把 LegacyMoney 配置里的 `pay_tax` 设为 `0`。** 本模组读不到那个值（LegacyMoney 没有导出它的
> getter），只能启动时打一条 WARN 提醒；若 `pay_tax` 仍大于 0，每笔转账的收款方都会被再扣一次税，
> 店主会被抽两次，退款补偿也会与实际到账不一致。
>
> 商店税只在**能把钱移出经济系统**的后端上生效（目前只有 LegacyMoney）。记分板没有这种账户，
> 所以在记分板上 `tax.shopTaxRate` 不生效，启动日志会写明这一条。

### 配置项（P0 已生效）

`adminShopName`、`shopContainers`、`maxShopAmount`、`allowPartialTransactions`、`blockSellPriceHigherThanBuy`、
`allowOwnerOpenChest`、`shopInteractionCooldownMs`、`ignoreCreativeMode`、`economy.backend`（`auto` /
`legacy_money` / `scoreboard` / `null`）、`economy.scoreboardObjective`、`economy.createObjectiveIfMissing`、
`economy.requireOwnerOnline`、`economy.currencySymbol`、`tax.shopTaxRate`。
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
