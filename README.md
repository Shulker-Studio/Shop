# Shop

基岩版（LeviLamina）告示牌箱子商店，复刻 Java 版 [ChestShop-3](https://github.com/ChestShop-authors/ChestShop-3) 的核心玩法。

> 当前状态：**模板初始化完成，功能未实现**。设计与实现计划见 [DESIGN.md](./DESIGN.md)。

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

经济后端按 `LegacyMoney → 记分板 → 空实现` 的顺序自动探测，见 `DESIGN.md` 第 8 节。

## 构建

```bash
xmake f -a x64 -m release -p windows -y
xmake -y
```

产物在 `bin/Shop/`。

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
