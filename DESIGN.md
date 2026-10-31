# ChestShop for Bedrock（LeviLamina）设计文档

> 目标：把 Java 版 ChestShop-3 的「告示牌 + 箱子」商店复刻到基岩版，作为 LeviLamina 原生模组实现。
> 本文档只描述设计与接口契约，不含实现代码。实现阶段按第 12 节分阶段推进。

- 参考实现：`ChestShop-authors/ChestShop-3`（LGPL-2.1），本机参考副本 `本地副本`，线上仓库 <https://github.com/ChestShop-authors/ChestShop-3>
- 模组工程：`Shop`（LeviLamina 模组模板，依赖 `levilamina 26.20.*`）
- 接口参考：`LeviLamina 源码`（main，v26.51.6）
- 命名空间：`ss::shop`（沿用模板的 `ss` 前缀）

---

## 1. 范围

### 1.1 本期做（P0）

| 能力 | 说明 |
| --- | --- |
| 告示牌商店解析 | 四行文本 → 商店对象；格式非法即"不是商店" |
| 创建 / 编辑 / 销毁 | 玩家写完牌子后校验，通过则规范化回写；校验失败默认提示并保留原牌（可配置为打掉） |
| 买卖交易 | 表单选数量，校验余额/库存/背包空间，预校验 + 快照 + 双向补偿 |
| 商店箱保护 | 非店主不能开箱、不能拆箱、不能拆牌 |
| 牌面防护 | 非店主不能编辑、染色、上墨、上蜡/除蜡（见 9.2） |
| 破坏防护 | 爆炸 / 活塞 / 凋灵 / 火焰不吞掉商店箱，旁边方块照常（方块级，见 6.2、ADR-0004） |
| 经济后端 | LegacyMoney / 记分板 / 空实现，自动探测（见 8.4） |
| 管理员商店（有容器） | 第一行 `Admin Shop`，库存来自容器；无容器的无限库存模式放到 P1（依赖经济方案） |
| 双箱 | 两半合并为 54 格库存 |
| 商店信息展示 | 点牌弹表单显示店主、商品、价格、库存、余额 |

### 1.2 本期不做（P1 / P2）

- 交易流水入库、平均价格（P1）
- 悬浮字、掉落物展示（P2，基岩版无原生文字展示实体，需客户端模组或假实体）
- 商店创建费 / 退款、管理员商店的无容器模式、shift 快捷成交（P1）
- 多语言、GUI 图标（P2）

### 1.3 明确不做的

- 不引入 Bukkit 式插件 API（事件总线只发布本模组事件）
- 不做通用箱子锁（`ChestLock`），只做"被商店占用的容器"的保护
- 不兼容 Java 版存档数据（两套存档互不相干）

---

## 2. 与 Java 版的关键差异

| 维度 | Java 版 ChestShop-3 | 基岩版本项目 | 影响 |
| --- | --- | --- | --- |
| 创建入口 | `SignChangeEvent`（写完牌子） | 监听 `PlayerEditSignBeforeEvent`（iListenAttentively），底层是 `BlockActorDataPacket` hook | 已确认可行，细节见 6.1 |
| 左右键语义 | 右击买、左击卖 | `PlayerAttackBlockBeforeEvent` 能捕获左键，但左键与挖掘/攻击冲突 | 默认「点牌 → 弹表单」；左键卖可作为 P1 选项 |
| 经济 | Vault / Reserve | 待定，先留 `EconomyProvider` 契约 | 见 8 |
| 容器保护 | 内建 + 可选 LWC | 自建「商店箱保护」 | 见 9 |
| 漏斗防偷 | 内建（监听 `InventoryMoveItemEvent`） | 需 hook 漏斗抽取路径 | P1 |
| 双箱库存 | Bukkit `DoubleChest` 合并 | `ChestBlockActor::mLargeChestPaired` 手动合并 | 见 7.3 |
| 告示牌双面 | 无（Java 1.20 才有双面文本，插件只读正面） | `SignTextSide::Front / Back` | 只看 `Front`，背面不参与逻辑 |
| 行宽限制 | 15 个空格宽的像素 | 客户端限制，按字符/宽度待实测 | 见 6.5 |

---

## 3. 术语

| 术语 | 定义 |
| --- | --- |
| **商店牌（Sign）** | 四行文本符合格式、且能连到容器的告示牌方块 |
| **容器（Container）** | 可作为商店库存的方块（默认 chest / trapped_chest，可配 barrel） |
| **店主（Owner）** | 商店牌第 1 行对应的玩家；`Admin Shop` 表示服务器 |
| **管理员商店** | 第 1 行为配置的 `adminShopName`（默认 `Admin Shop`），无限库存/无限资金 |
| **B 价 / S 价** | 玩家**买入**单价 / 玩家**卖出**单价；同一商店可只有一边 |
| **数量（Quantity）** | 牌子第 2 行，每笔交易的基础数量 |
| **商店箱** | 被某块有效商店牌连接的容器（双箱算一个整体） |

---

## 4. 数据源与生命周期

### 4.1 牌子即数据源（照搬 Java 版）

- **没有商店注册表**。是否商店、店主是谁、卖什么、价格多少，全部由牌子四行 + 相邻容器实时推导。
- 服务器重启无需迁移：牌子还在，商店就在。
- 牌子上只有展示文本，不存 NBT 自定义数据（基岩版方块实体自定义字段在 vanilla `save` 中不可靠）。
- **P0 不建索引**。每次点击解析一次；每次保护检查读周围 5 个方块实体。成本可接受。
- P1 若做悬浮字/统计，再引入 `ShopIndex`（`维度 + BlockPos → ShopRef`），由创建/销毁事件增量维护；**索引永远可由牌子重建**，不作为数据源。

### 4.2 生命周期

```
        写牌子（编辑提交，PlayerEditSignBeforeEvent）
                     │
                     ▼
        ┌──────────────────────────┐
        │ 这块牌的正面已有商店？     │
        └───────────┬──────────────┘
              是     │              否
        ┌────────────┘              └─────────────┐
        ▼                                        ▼
  编辑者是店主或 OP？                    解析四行 + 找容器
   ├─ 否 → 取消包 + 提示"这是 X 的商店"   ├─ 失败 → 提示原因 + 保留原牌
   └─ 是 → 继续校验（同右侧）              └─ 成功 → 规范化回写 → 商店生效
```

规则：
- 拆牌 = 关店，箱子立刻恢复普通箱子，无需任何清理。
- 拆箱 = 连带关店（箱子没了，商店牌随之物理掉落）。
- 空店默认**不自动删**（对齐 `REMOVE_EMPTY_SHOPS = false`）。
- 商店数据不写盘，所以不存在"孤儿记录"。
- **编辑、染色、荧光墨囊、墨囊、上蜡、除蜡一律要求店主或 OP**；非店主在交互阶段就被拦下（不弹编辑 UI），提交阶段再兜底取消一次。见 10.1。
- **上蜡不改变商店语义**。它是引擎级的编辑冻结，持久化在方块实体里，玩家可选；我们只把"除蜡"纳入店主权限，防止别人一斧头解锁再改。
- 背面的任何书写都不影响商店，见 5.5。

---

## 5. 告示牌文本规范

### 5.1 四行

| 行 | 内容 | 权威性 | 规则 |
| --- | --- | --- | --- |
| 1 | 店主显示名 / `Admin Shop` | 显示 | 服务器规范化时用**正面编辑者 XUID 的当前名字**覆盖。写 `Admin Shop` 且编辑者是 OP → 管理员商店（见 7.4） |
| 2 | 份 | **权威** | 整数 `1 .. maxShopAmount`（默认 3456），表示一次交易的份量；牌上价格是**一份的总价**（对齐 Java 版默认语义）。**不支持** Java 的 `Q 64 : C 64` 计数格式 |
| 3 | 价格 | **权威** | `B <价> : S <价>`，B/S 至少一个；支持只写 `B <价>` 或 `S <价>`、`<价>`（视为 B）、`free` |
| 4 | 商品显示名 | 显示 | 权威是绑定容器里**第一个非空槽位**的物品类型；规范化时用该物品的展示名覆盖。玩家写 `?`、留空或写别的名字都可以 |

即：**第 2、3 行是玩家说了算的，第 1、4 行是服务器说了算的**。

### 5.1.1 什么才算「商店牌」（关键）

**普通告示牌绝不能被本模组改写或打掉。** 判定顺序与 Java 版一致：

1. 第 3 行去颜色码后必须含 `B` 或 `S`（大小写不敏感），否则**直接放行**，不做任何处理；
2. 第 1 行去颜色码后必须非空；
3. 满足 1、2 才进入第 5.2 节的完整校验链。

即：先判断「像不像商店」，再判断「是不是合法商店」。像但不是（数量非法、价格非法、没容器……）时，默认只提示原因并**保留原牌**；`removeInvalidShopSign` 默认 false，永远不主动删玩家的牌子。

> 第 1 行为 `Admin Shop` 的牌子仍受同样的文本规则约束，并且**同样需要容器**（D11）：P0 不允许凭空发货或收钱。

### 5.2 价格行文法（简化的 Java 版）

```
price_line  := segment (":" segment)?
segment     := [B|S] amount | amount [B|S]
amount      := number | "free"
number      := digits [ "." digits ] [ "K" | "M" ]   // K=千, M=百万，可选
```

- 只有一段时按 B 处理（对齐 Java 版 `PriceUtil`）。
- `B 价` 与 `S 价` 都缺失 → 非法。
- 允许 `S > B` 吗？默认拒绝（对齐 `BLOCK_SHOPS_WITH_SELL_PRICE_HIGHER_THAN_BUY_PRICE = true`），可配置。
- 价格精度：整数（基岩版经济若为浮点，见 8.2）。小数按**向下取整**处理（`B 1.5` = 1，`B 1.5K` = 1500），小数位最多 9 位，更多位视为非法。
- `free` 与 `0` 等价；零价商店合法。规范化时零价写成 `B 0`，不写 `free`。
- `K`/`M` 与 `B`/`S` 都是大小写不敏感（对齐 Java 版先 `toUpperCase` 再解析），规范化时一律写大写。
- 第 2 行不 trim、不允许任何空白，但会先去颜色码（和 1、3 行对齐）；解析出的金额必须落在 `int64` 内，溢出按非法处理。

### 5.3 物品行

**商品不由牌面文本决定**，这是与 Java 版最重要的偏离（Java 按牌上的物品名反查物品）。

- 商品 = 绑定容器里**第一个非空槽位**的物品类型。玩家在牌上写什么都改变不了它。
- 第 4 行只是显示。规范化时用容器里那件物品的展示名覆盖，超宽截断，全名在表单里看。
- 双箱没有统一的全局槽位顺序：第 4 行取"附着那一半的第一个非空槽位，没有再取另一半"。
- `?` 仍然接受，含义是"让服务器填"，和留空、写任意名字等价。
- 因为没有"解析物品名"这一步，校验链里**不存在"物品非法"这个失败分支**；唯一的失败情况是**容器为空**（没有商品可卖），提示"请先在箱子里放商品"。
- 商品比较：按物品类型 + aux + 用户数据（附魔、耐久、自定义名）严格比较，避免"同名不同物"。玩家卖进来的物品必须与容器里的商品严格匹配才收。
- 因此一个商店只能卖一种物品。混装箱子里只有第一个非空槽位的类型参与交易，其余格子视为库存的一部分但不会被交易。

### 5.4 规范化回写

优先走「改包」而不是「事后写回」：

```
PlayerEditSignBeforeEvent
  ├─ 解析 text()（该面的完整多行文本，四行之间是真正的换行符 `\n`）
  ├─ 校验失败 → setCancelled(true)，包被丢弃，牌子保持原样（或按配置打掉）
  └─ 校验通过 → text() = 规范化后的四行
                 → origin() 发出，服务端与客户端同时得到规范化文本
```

依据：`PlayerEditSignBeforeEvent::text()` 返回 `std::string&`，绑定的是 `pPacket->mData["FrontText"]["Text"]`（`StringTag` 是 `std::string` 的子类），所以修改它等于修改待写入的 NBT。iListenAttentively 在 `origin()` 之前发布 Before 事件，取消即 `return`。

好处：
- 不需要 `setMessageForServerScripingOnly` + `BlockActor::refresh`，少一个 hook 点；
- 服务端存档与客户端显示天然一致，不会出现"服务端已改、客户端还是旧文本"的窗口。

规范化内容：

- 第 1 行：店主名（管理员店用配置名）；
- 第 2 行：数量；
- 第 3 行：`B <价> : S <价>`（缺省的一边省略）；
- 第 4 行：商品名。

若最终仍需直接改已存在的方块实体（例如外部工具写入、或未走包路径的编辑），回退方案是 `SignBlockActor::setMessageForServerScripingOnly(SignTextSide::Front, text, ownerId)` + `BlockActor::refresh(blockSource)`；`ownerId` 传编辑者 XUID，未知时传空串（语义待实测）。

**P0 的实际做法**：包路径不保证写入 `mLastEditXuid`（见 6.5 第 3 条），所以在 `PlayerEditSignAfterEvent` 里**只补写 ownerID、不改文本**——正面已是合法商店且记录的 XUID 与本次编辑者不一致时，用当前文本调一次 `setMessageForServerScripingOnly(Front, 现有文本, 编辑者 XUID)`，不 `refresh`（客户端已经通过包拿到同一份文本）。普通牌、非法牌、附着容器不可用的牌一律不写。

### 5.5 只看正面，背面是布告板

`SignTextSide::Front` 参与逻辑，`Back` 完全不参与：服务器不读、不写、不校验。玩家在背面写公告、批发价、店规都可以；即使在背面写出一套合法的商店格式，也不会被识别成第二个商店（ADR-0006）。

一块告示牌最多是一个商店。

---

## 6. 事件与 Hook 清单

### 6.1 结论先行

`LiteLDev/iListenAttentively`（LGPL-3.0，LL ≥ 26.51.7）已经实现了本模组需要的大部分 hook，**H1 不再是高风险项**。它的 `PlayerEditSignBeforeEvent` 源码直接给出了此前所有未知细节：

| 此前的未知项 | 实际答案（来自 `PlayerEditSignEvent.cpp`） |
| --- | --- |
| 告示牌包怎么识别 | `mData->contains("id", Tag::Type::String) && mData["id"] == "Sign"` |
| 文本 NBT 键名 | `FrontText["Text"]`、`BackText["Text"]` |
| 玩家身份 | `thisFor<NetEventCallback>()->_getServerPlayer(pSource, pPacket->mSenderSubId)`，`BlockActorDataPacket` 本身带 `mSenderSubId` |
| 时序 | Before 事件在 `origin()` **之前**发布，此时 `SignBlockActor` 里仍是旧文本，包里是新文本，正好用来判断"这一面是否被编辑" |
| 取消语义 | `beforeEvent.isCancelled()` 后直接 `return`，不调 `origin()`，即**丢弃整个包**，牌子不更新 |
| 现有文本读取 | `blockActor->mTextFront->getMessage()` / `mTextBack->getMessage()` |
| 判断"编辑了哪一面" | 包内文本与 `mTextFront->getMessage()` / `mTextBack->getMessage()` 分别比较 |

### 6.2 仍然需要自己 hook 的（H2 漏斗、H3 方块级破坏）

| 编号 | 目标 | 用途 | 风险 |
| --- | --- | --- | --- |
| H2a | `Hopper::_tryPullInItemsFromAboveContainer(BlockSource&, Container&, Vec3 const&)`、`Hopper::_pushOutItems(BlockSource&, Container&, Vec3 const&, int)` | 漏斗**方块与漏斗矿车**从商店箱抽货 / 往商店箱塞货（两者共用 `Hopper` actor 的抽取逻辑，hook 一次覆盖两者） | 中（iListenAttentively 无漏斗事件；ChestLock 已验证写法） |

| H2c | `TransportItemsGoal::tick` / `_executeEvent`（`v1_21_100` 的新 AI goal，经 `ChestBlockActor::ChestCloser` 开箱取放） | 铜傀儡从商店箱取货 / 往商店箱放货 | 中（goal 粒度，取消会影响该傀儡的所有运输；也可能误伤同样使用该 goal 的其他生物） |
| H2d | `DropperBlockActor::pushOutItems(BlockSource&)` | 投掷器往商店箱塞货 | 低 |
| H3a | `ExplosionStartedEvent::mBlocks`（`TypedStorage<8,64,std::unordered_set<BlockPos>>`），由 `BlockEventCoordinator::sendEvent` 拦截 | 爆炸时把商店箱从方块列表里**剔除**，而不是取消整场爆炸 | 中（同 ChestLock 已验证） |
| H3b | `PistonBlockActor::_attachedBlockWalker(BlockSource&, BlockPos const&, uchar, bool)` | 活塞推动路径上遇到商店箱时返回 false | 中 |
| H3c | `WitherBoss::_destroyBlocks(Level&, AABB const&, int, WitherAttackType, BlockSource&)` | 凋灵破坏范围内含商店箱时提前返回 | 中 |
| H3d | `FireBlock` 燃烧判定 / `BlockEventCoordinator` 的 burn 分支 | 火焰不吞掉商店箱 | 中 |
| H3e | `PlayerDestroyBlockEvent` + 扫描周围附着的商店牌（对齐 Java `getAttachedSigns`） | **拆掉商店牌依附的方块**会让牌子物理掉落、商店消失，这是非店主绕过"不能拆牌"的真实路径 | 低（逻辑无需引擎内部符号，属于纯判定） |

> H3 之所以不用 iListenAttentively 的 `ExplosionBeforeEvent` / `PistonPushBeforeEvent` / `WitherDestroyBeforeEvent` / `FireTryBurnBlockBeforeEvent`，是因为那些事件只能**取消整场**爆炸/推动/燃烧，粒度不对（ADR-0004）。这四个 hook 的符号在 LeviLamina 26.51.8 头文件中均已确认存在，写法参照 ChestLock。

### 6.3 直接监听即可（iListenAttentively 提供，无需自己 hook）

| 事件 | 用途 | 可取消 |
| --- | --- | --- |
| `ila::mc::world::actor::player::PlayerEditSignBeforeEvent` | 告示牌提交：创建/编辑商店、校验、规范化回写 | 是（丢弃包） |
| `ila::mc::world::actor::player::PlayerEditSignAfterEvent` | 牌子已更新后的通知（日志/索引刷新） | 否 |
| `ila::mc::world::actor::player::PlayerOpenContainerBeforeEvent` | 阻止非店主开商店箱；`pos` / `containerType` / `containerActorId` 齐全 | 是 |
| `ila::mc::world::actor::player::PlayerAttackBlockBeforeEvent` | 左键点方块（可做"左键=卖"，也用于提前拦截拆牌） | 是 |
| `ila::mc::world::actor::player::PlayerDestroyBlockEvent`（LL 原生） | 拆牌/拆箱保护 | 是 |
| `ila::mc::world::ChestPairWithBeforeEvent` | 双箱配对时校验两半归属 | 是 |
| `ila::mc::world::ExplosionBeforeEvent` | 爆炸保护（`explosion()` 可取受影响方块） | 是 |
| `ila::mc::world::PistonPushBeforeEvent` | 活塞推商店箱/牌 | 是 |
| `ila::mc::world::WitherDestroyBeforeEvent` | 凋灵破坏 | 是 |
| `ila::mc::world::FireTryBurnBlockBeforeEvent` | 火烧商店箱/牌 | 是 |
| `ila::legacyMoney::MoneyChangeBeforeEvent` | 观测/拦截 LegacyMoney 的 Set/Add/Reduce/Trans | 是 |
| `ila::mc::server::ReceivePacketBeforeEvent<BlockActorDataPacket>` | 备选：若不想依赖 `PlayerEditSignBeforeEvent`，可自行过滤 | 是 |

### 6.4 依赖方式（已定：依赖 iListenAttentively）

- 编译期：`add_requires("ilistenattentively")` + `add_packages("ilistenattentively")`。xmake-repo 已收录该包（`packages/i/ilistenattentively/xmake.lua`），当前 `open` 最高版本为 **0.17.0**（要求 LL `26.51.*`），安装时会从源码编译并安装头文件 + 导入库。
- 运行时：`ILAPI` 展开为 `LL_SHARED_IMPORT`，即链接其 DLL 的导入表。**iListenAttentively 未安装时不是"功能缺失"，而是进程加载器直接失败**（Shop 整个 mod 起不来）。
- 因此必须在 `tooth.json` 声明运行时依赖 `github.com/LiteLDev/iListenAttentively`，并在文档/安装说明里写清楚。
- 版本口径：**实际构建基线为 iListenAttentively `0.17.0` + LeviLamina `26.51.8`**（`xmake f` 解析结果）；运行时不低于 0.17.0。0.17.2 起其 `tooth.json` 要求 LL `>= 26.51.7`，发行声明按此写。
- 不复制其源码（LGPL-3.0）；只 `#include` 其头文件并链接其导出符号。
- 不做"未安装则降级"——因为链接方式决定了根本降级不了，只能要求安装。

### 6.5 仍需实测的两项

1. 基岩版告示牌单行实际可容纳字符数/宽度（决定物品名与价格行格式）。
2. 表单回调线程与方块操作的线程安全性（表单回调可能不在主线程）。
3. **包路径是否会写入 `mLastEditXuid`**。`setMessageForServerScripingOnly(side, text, ownerID)` 需要显式传 `ownerID`，说明 owner 不来自包；若引擎不写，ADR-0005 的店主判定需要在实际写入后补一次显式写入。

---

## 7. 绑定规则（比 Java 版严格：只认附着）

Java 版的 `uBlock.findConnectedContainer` 是「墙牌先看背面，然后按 `SELF / 下 / 上 / 东 / 北 / 西 / 南` 扫六面」，**不要求牌子真的接触箱子**——立在箱子旁边地上的牌子也会连上隔壁的箱子。基岩版放弃这套宽松规则。

### 7.1 找容器

商店牌必须**附着**在容器的六个面之一。判定只读牌子自己的附着目标，不扫邻居：

| 牌子形态 | 附着目标 | 是否成为商店 |
| --- | --- | --- |
| 墙牌（贴在方块侧面） | 背面那一格 | 该格是容器才算 |
| 站立牌（立在方块顶面） | 下方那一格 | 该格是容器才算 |
| 悬挂牌吊在方块底面 | 上方那一格 | 该格是容器才算 |
| 悬挂牌挂在方块侧面 | —— | P0 不支持，见下 |

- 站立牌立在**箱子旁边的地上** → 下方是地板，不是容器 → 不是商店。
- 墙牌贴在**箱子旁边的墙上** → 背面是墙，不是容器 → 不是商店。
- **侧挂的悬挂牌 P0 不认**：基岩版把两种悬挂形态做成同一个方块 ID（`minecraft:oak_hanging_sign`），侧挂时 `facing_direction` 与墙面垂直，方块状态里没有"挂在哪一面墙"的信息（PowerNukkit 的 `BlockHangingSign` 同样只能扫四周，且它的 `place` 用 `face.rotateY()` 存朝向）。猜错会把牌子绑到**旁边的另一个箱子**上，所以宁可拒绝并提示"牌子要贴在或立在箱子上"。吊在方块底面（`hanging == true`）的形态信息明确，正常支持。
- 找不到附着容器 → `NO_CHEST`（管理员店同样适用，见 7.4）。

**一对一**：一块牌只绑定它附着的那一个容器；一个容器也只被一块牌绑定。牌子旁边的其他箱子对这块牌不存在，仍是普通箱子——不存在"一块牌管一片箱子"的可能。唯一例外是双箱：原版本来就把两个方块当作一个容器，合并成 54 格。

可作容器的方块（配置 `shopContainers`）：`minecraft:chest`、`minecraft:trapped_chest`，默认不含 barrel（可加）。

判定方式：附着格的方块实体非空、`isTypeOrDerived(BlockActorType::Chest / BarrelBlock)`，**并且** `Block::getTypeName()` 在 `shopContainers` 白名单里。两条都满足才算容器——只匹配名字但读不到容器内容，不能当商店。白名单不匹配时单独提示"这种容器不能当商店用"，不和"牌子没贴对"混在一起。

墙牌的朝向从方块状态的 `facing_direction`（2 北 / 3 南 / 4 西 / 5 东）读出，退化时读 `cardinal_direction`；**朝向 = 牌子正面朝外的方向，附着点在它的反方向**（与 PowerNukkit `BlockWallSign::onUpdate` 用 `getBlockFace().getOpposite()` 找支撑方块一致）。状态名在方块状态表里按 `minecraft:` 前缀兼容比对，LeviLamina 没有导出 `SignDirection` 符号。

### 7.2 多商店规则

因为绑定只认附着，每条规则都变得可以肉眼验证：

- **一个容器同时只能被一个商店牌绑定**。第二块牌附着到同一容器时拒绝创建，提示"这个箱子已经有商店了"。
- 双箱的两半是**不同的附着目标**：牌子贴在哪一半，就绑定那一半，然后按 7.3 合并成 54 格。两块牌分别贴双箱两半 → 它们争的是同一个 54 格库存，按上一条拒绝。
- 一个方块最多有六个面，被容器占掉的面挂不了牌子，其余面的背后不是容器 → 结构上不可能出现"同一附着方块挂多块牌"的情况，原先那条规则删除。
- 创建时不需要再"检查周围所有可作容器的方块是否可访问"，只需要检查**附着的那一个容器**。

**提示文案是这条规则的成败关键**：玩家把牌子立错位置时必须明确说"牌子要贴在或立在箱子上"，否则他会以为是模组坏了。

### 7.3 双箱

- `ChestBlockActor::mLargeChestPaired`（`ChestBlockActor*`）指向另一半；`mLargeChestPairedPosition` 是坐标。
- 牌子附着在哪一半就由哪一半触发配对，配对后按 54 格整体处理。
- 合并库存 = 两半各 27 格，共 54 格；操作时分别 `setItem` 并各自 `refresh`。
- 若配对未完成/另一半不存在 → 按单箱处理。
- 保护逻辑对双箱两半都生效。

### 7.4 管理员商店

- 第 1 行等于 `adminShopName`（默认 `Admin Shop`）→ 管理员商店。
- **P0 默认要求管理员店也必须有容器**，库存来自容器，避免在经济未定时出现"凭空发货/收钱"的桩行为。
- 无容器的无限库存模式（Java 版 `AdminInventory`）放到 P1，等经济后端方案确定后实现。
- 若挂了容器且 `FORCE_UNLIMITED_ADMIN_SHOP = false`（默认）→ 用容器库存。
- 资金端：服务器账户（全 0 UUID）随 P1 的经济方案落地。

### 7.5 创建时的放置姿势

基岩版把牌子放到箱子上需要**潜行 + 放置**（否则会先打开箱子）。墙牌贴箱子侧面、站立牌立箱子顶面都是这个姿势。文档与提示文案都按这个姿势引导；`PlayerPlaceBlockEvent` 只用于 P1 的预检查，不参与判定。

---

## 8. 经济接口契约

不依赖 [`IceBlcokMC/EconomyBridge`](https://github.com/IceBlcokMC/EconomyBridge)（LGPL-3.0），但**接口形状与其对齐**，方便将来替换或迁移。后端实现由本模组自己提供，不引入第三方静态库。

### 8.1 接口

> **P0 实现的实际形状（04 / 05 落地后）**，与下面这段签名的四处差异：
> 1. 账户键用 `Account`（就是 **XUID** 字符串），不用 `mce::UUID`——纯逻辑层不得包含 `mc/` 头，
>    而店主身份（ADR-0005）与 LegacyMoney 本来就以 XUID 为键，用 UUID 反而要多一次反查；
> 2. 没有 `getSymbol()`：货币符号是商店的展示属性，只从 `economy.currencySymbol` 读（见 8.7 第 6 条），
>    后端再提供一个 getter 只会造出第二个来源；
> 3. `transfer` 不抛异常：本工程不开异常（`set_exceptions("none")`），补偿失败返回
>    `TransferResult::Broken`，由调用方记严重日志、通知 OP 并冻结该次操作。
> 4. `get` 返回 `std::optional<std::int64_t>`：**空 = 这个账户现在读不到**，`0` 就是真的 0 元。
>    卖出方向（05）必须分清这两件事，否则会把「读不到店主余额」误报成「店主资金不足」（见 8.2）。

```cpp
namespace ss::shop::economy {

class EconomyProvider {
public:
    using Ptr = std::shared_ptr<EconomyProvider>;

    virtual ~EconomyProvider() = default;

    virtual std::string_view getName() const   = 0;
    virtual std::string_view getSymbol() const = 0;

    // 能力协商：能否为离线玩家记账。
    // 记分板后端返回 false（只处理在线玩家）；自建账本/LegacyMoney 返回 true。
    virtual bool supportsOffline() const = 0;

    virtual std::int64_t get(mce::UUID const& player) const = 0;
    virtual bool         set(mce::UUID const& player, std::int64_t amount) = 0;
    virtual bool         add(mce::UUID const& player, std::int64_t amount) = 0;
    virtual bool         reduce(mce::UUID const& player, std::int64_t amount, bool allowNegative = false) = 0;

    // 默认实现：reduce + add，add 失败时把 reduce 的钱加回去；回滚也失败则抛异常。
    virtual bool transfer(
        mce::UUID const& from,
        mce::UUID const& to,
        std::int64_t     amount,
        bool             allowNegative = false
    );
};

} // namespace ss::shop::economy
```

### 8.2 语义约定

- `amount` 必须 > 0；税费由调用方分摊，接口不感知。
- `reduce` 在 `allowNegative == false` 且余额不足时返回 `false`，且**余额不变**。
- `get` 在账户不可记账时返回空（不在线、后端没有这个账户、记分板目标不存在），
  **不能拿 0 顶替**：买入方向无害（买家必在线），卖出方向会把「读不到店主余额」变成
  「店主资金不足」——一个有钱的店主被报缺钱，事故无从查起。调用方读不到时整笔拒绝，
  文案见 8.5。
- `transfer` 失败时保证双方余额都不变（内部补偿）；补偿本身失败返回 `TransferResult::Broken`
  （不抛异常，见 8.1 第 3 条），调用方按第 10.2 节当作严重错误处理。
- 后端实现需线程安全（表单回调可能在非主线程）。
- 金额精度：整数；记分板后端以 1 分 = 1 单位。
- **金额上限**：记分板后端内部是 `int`（32 位），超过 `INT32_MAX` 会溢出成负数。所有后端统一按 `int64` 处理，但记分板实现在写入前必须检查范围并拒绝，不能静默截断。
- 服务器账户（管理员店收款、税）：保留 UUID（全 0）表示，P1 实现。

### 8.3 参考实现与取舍

| 能力 | EconomyBridge | LegacyMoney | 本模组 |
| --- | --- | --- | --- |
| 接口 | `get/set/add/reduce/transfer` | C 导出 `LLMoney_*`（XUID 为键） | 同 EconomyBridge 形状（UUID 为键） |
| 存储 | 无（薄适配） | SQLite `economy.db`，`money` + `mtrans` | 由后端决定 |
| 离线玩家 | 记分板后端不支持；LegacyMoney 后端支持 | **支持**（按 XUID 记账，与是否在线无关） | 由 `supportsOffline()` 声明 |
| 原子转账 | 自己补偿 | **SQLite 事务**（begin/commit/rollback） | 交给后端，接口只保证"失败不变更" |
| 税率 | 无 | `pay_tax`（转账时扣） | 不重复收税（见 8.7） |
| 事件 | 无 | `LLMoney_ListenBeforeEvent/AfterEvent`（可拦截） | 不依赖，但需容忍被别人拦截 |

借鉴的是接口形状与调用方式，**不复制源码**（EconomyBridge 为 LGPL-3.0，LegacyMoney 的许可证需按其仓库为准；本工程 AGPL-3.0）。

### 8.4 后端探测顺序

> **P0 阶段性现状（04 落地后）**：本模组目前只有 `ScoreboardEconomy` 与 `NullEconomy`。
> `auto` 实际等价于「记分板目标存在就用它，否则降级为 Null 并报错」；
> 显式写 `legacy_money` / `builtin` 属于**配置错误，拒绝加载**（而不是静默降级——
> 服主写了那个名字就是要它记账，拿记分板顶替会把余额落到他没在看的地方）。
> 下面的 1、2、3 是 ticket 10 的目标状态；`createObjectiveIfMissing` 同样在 10 才生效。

1. 配置文件显式指定 `economy.backend`：`auto` / `legacy_money` / `scoreboard` / `builtin` / `null`。
2. `auto` 的判定顺序：
   1. `GetModuleHandle(L"LegacyMoney.dll")` 非空且 `LLMoney_*` 导出符号齐全 → `LegacyMoneyEconomy`（**首选**，支持离线、事务、税率、事件）。
   2. 否则若记分板目标存在 → `ScoreboardEconomy`（仅在线）。
   3. 否则 → `NullEconomy` 并打警告（仅开发用；生产环境应视为配置错误）。
3. `builtin`（自建 SQLite 账本）作为 P1，给不装 LegacyMoney 的服务器用；接口相同，`supportsOffline()` 返回 true。
4. 加载失败不 panic：降级到下一个候选，并把最终选择写进日志与 `/shop info`。

> 生态事实：LegacyScriptEngine 的 `tooth.json` 依赖 `LegacyMoney`，所以**装了 LSE 的服务器一定有 LegacyMoney.dll**。`auto` 在多数生产服会直接命中它。

### 8.5 离线店主策略

判断依据是后端的 `supportsOffline()`，不是"店主此刻是否在线"。

| 情况 | 支持离线的后端（LegacyMoney / builtin） | 不支持离线的后端（Scoreboard） |
| --- | --- | --- |
| 买入（钱付给店主） | 正常成交 | **禁止成交**，提示「店主不在线」 |
| 卖出（店主付钱给买家） | 正常成交 | **禁止成交**，提示「店主不在线」 |
| 管理员店 | 服务器账户，不受影响 | 同左 |

- `Scoreboard` 后端下，点击牌子时就提前判定并禁用表单按钮，避免玩家填完数量才被拒。
- **余额读不到**（`get` 返回空，见 8.2）与「余额为 0」不同：点牌时正文写占位、不编造数字；
  真正要动钱的那一步之前会再确认一次两边账户（§10.2 第 2 步），读不到就整笔拒绝、两边余额未动，
  而不是「先扣款再退款」。不再在点牌阶段因此禁用按钮：读不到的只有账户，与这家店能不能收钱无关。
- 配置 `economy.requireOwnerOnline` 只影响 `Scoreboard` 分支；对支持离线的后端无意义。

### 8.6 P0 的实现清单

1. `ScoreboardEconomy`：`Scoreboard::modifyPlayerScore` + `PlayerScoreSetFunction`，与 EconomyBridge 同路数；**写入前检查 `INT32` 范围**。
2. `LegacyMoneyEconomy`：`GetProcAddress` 解析 `LLMoney_Get/Set/Add/Reduce/Trans`；UUID → XUID 用 `ll::service::PlayerInfo::fromUuid`。
3. `NullEconomy`：全成功、不记账，仅开发。
4. `EconomyManager`：探测、降级、日志、`/shop economy` 状态输出。
5. `builtin` 与流水/均价留到 P1。

### 8.7 LegacyMoney 对接细节（实现时必须遵守）

1. **键是 XUID 不是 UUID**：`PlayerInfo::fromUuid(uuid).xuid`。查不到 XUID（从未进服的账号）时视为「无法记账」，交易失败并提示。
2. **必须用 `LLMoney_Trans` 完成买卖**，不要用 `Reduce` + `Add` 两条独立调用：
   - `LLMoney_Trans` 内部是 SQLite 事务，天然原子；
   - `LLMoney_Trans` 会应用 `pay_tax`，自己拼两条调用会漏税或重复扣税；
   - `Reduce`/`Add` 本身也是 `Trans` 的包装，`from == to` 会被拒（阻止店主自买自卖）。
3. **`LLMoney_Get` 有写副作用**：查询不存在的 XUID 会插入 `def_money` 记录。所以 `get()` 不能当作纯读，不要在只读展示路径上频繁调用（缓存一次即可）。
4. **`allowNegative` 无法支持**：LegacyMoney 永远拒绝负余额，`allowNegative = true` 时按 `false` 处理并记 debug 日志。商店不使用该参数。
5. **可能被第三方拦截**：`LLMoney_ListenBeforeEvent` 允许其他插件取消转账，`LLMoney_Trans` 会返回 `false`。商店必须把 `false` 当作正常失败路径（提示"交易被服务器阻止"，无副作用）。
6. **货币符号拿不到**：LegacyMoney 的 `currency_symbol` 在它自己的配置文件里，跨模组读不到。`getSymbol()` 用 Shop 自己的配置项（默认 `$`），与 LegacyMoney 配置保持一致由服主负责。
7. **金额是 `long long`**，无 int32 问题；但 `Scoreboard` 后端有，`EconomyManager` 要按后端分别限制上限。
8. **`pay_tax` 会被叠加且无法读取**：`LLMoney_Trans` 每次转账都从收款方扣 `pay_tax`，而 LegacyMoney 没有导出该配置的 getter。商店税与 `pay_tax` 同时开启会让店主被二次抽税、补偿金额漂移。P0 规则：`shopTaxRate > 0` 时无条件 WARN 要求服主手动设 `pay_tax = 0`，否则商店税按 0 处理（见 8.9）。

### 8.8 LeviLamina 版本谱系（必须同步）

| 目标 | LeviLamina | 说明 |
| --- | --- | --- |
| 本工程模板 | `26.20.*` | 模板默认 |
| EconomyBridge | `26.51.5` | 参考实现 |
| LegacyScriptEngine 0.22.3 | `>=26.51.2 <26.60.0` | 依赖 LegacyMoney 0.22.* |
| iListenAttentively 0.17.2（发行要求） | `>=26.51.7 <26.60.0` | 运行时依赖按此声明 |
| **实际构建基线** | LeviLamina `26.51.8` + iListenAttentively `0.17.0` | `xmake f` 实际解析的版本，事件 API 以它为准 |
| 旧 ChestShop / ChestLock | `1.6.1` | 旧工程 |

**已决定**：Shop 的 `levilamina` 目标提到 **`26.51.7+`**，同时满足 iListenAttentively（≥ 26.51.7）、LSE（≥ 26.51.2）与 EconomyBridge（26.51.5）。代价是 `ChestLock` 与旧 `ChestShop` 的代码不能直接搬（`BlockActor::getCustomName` 已移到 `VanillaBlockActor` 等），移植时按 26.51 的头文件重写。

---

### 8.9 商店税（与 `pay_tax` 叠加）

已决定：商店自己再收一道税，不依赖 LegacyMoney 的 `pay_tax`。

| 项 | 规则 |
| --- | --- |
| 税基 | 成交总额 `单价 × 实际数量`（部分成交按实际数量算） |
| 税率 | `shopTaxRate`，默认 `0`（百分比，`0.05` = 5%） |
| 承担方 | **店主承担**：买家总支出仍是 `total`，店主实收 `total - tax`，差额被系统回收 |
| 流向 | 默认**销毁**（从经济系统移除）；P1 可配 `taxDestination` 指向税收账户 |
| 舍入 | 向下取整到整数，余数留给店主，不出现负税 |
| 税为 0 | 完全跳过第二笔转账，不产生额外流水 |

**实现**（LegacyMoney 后端）：

```
1. Trans(buyer, owner, total - shopTax)   // 主交易
2. Trans(buyer, "", shopTax)              // 税：to 为空 = 系统回收
3. 任一步失败 → 按确定金额反向补偿（见 10.2）
```

**前提约束（P0 硬性要求）**：启用 `shopTaxRate > 0` 的前提是服主**手动把 LegacyMoney 的 `pay_tax` 设为 0**。

- LegacyMoney 没有导出 `pay_tax` 的 getter，Shop **无法**在运行时读取或检测这个值。
- 如果 `pay_tax > 0`，上面两笔 `Trans` 都会在收款方再抽一次：店主被抽两次（`total - tax` 再打折），且补偿金额会漂移（实际到账 ≠ 记账金额）。
- 因此启动时只要 `shopTaxRate > 0` 就**无条件打 WARN**，文案明确要求确认 `pay_tax = 0`；README 与安装说明同步写这一条。
- 若服主不关闭 `pay_tax`，P0 的行为是：`shopTaxRate` 被强制按 0 处理（禁用商店税），只保留 LegacyMoney 自己的税。
- 两笔 `Trans` **不是跨调用事务**：第二笔失败时第一笔已生效，必须按 `total - shopTax` 精确补偿；补偿本身也可能失败，按第 10.2 节升级为严重错误。

---

## 9. 商店箱保护（P0 核心）

### 9.1 保护对象

被至少一块有效商店牌连接的容器（含双箱两半）以及这些商店牌本身。

### 9.2 规则

| 行为 | 非店主 | 店主 | OP | 实现依据 |
| --- | --- | --- | --- | --- |
| 右键点商店牌 | 打开交易表单（取消原版编辑） | 打开店主表单 | 同店主 | `PlayerInteractBlockEvent` |
| 编辑商店牌文本 | **取消**（交互阶段就不弹 UI） | 允许 | 允许 | `PlayerInteractBlockEvent` + `PlayerEditSignBeforeEvent` 兜底 |
| 染色 / 荧光墨囊 / 墨囊 | **取消** | 允许 | 允许 | `PlayerInteractBlockEvent`（对照 `SignBlock::SignInteractionResult::UseDye` 等分支） |
| 上蜡 | **取消** | 允许 | 允许 | 同上（`UseHoneyComb`） |
| 除蜡（斧头） | **取消** | 允许 | 允许 | 同上 + `SignBlock::_canUseHoneyComb` 语义 |
| 右键点商店箱 | 取消（不能开箱） | 允许（可配 `allowOwnerOpenChest`，默认 true） | 允许 | `PlayerOpenContainerBeforeEvent` |
| 拆商店牌 | 取消 | 允许（拆除即关店） | 允许 | `PlayerAttackBlockBeforeEvent` + `PlayerDestroyBlockEvent` |
| 拆商店箱 | 取消 | 允许（连带关店） | 允许 | 同上 |
| 爆炸 / 活塞 / 凋灵 / 火焰 | 保护 | 保护 | 保护 | **方块级**：`ExplosionStartedEvent::mBlocks` 剔除、`PistonBlockActor::_attachedBlockWalker` 返回 false、`WitherBoss::_destroyBlocks` 提前返回（见 6.2、ADR-0004） |
| 漏斗方块 / 漏斗矿车抽塞 | 阻止 | 阻止 | 阻止 | H2a（见 6.2） |
| 铜傀儡取放货 | 阻止 | 阻止 | 阻止 | H2c |
| 投掷器塞货 | 阻止 | 阻止 | 阻止 | H2d |
| 拆商店牌依附的方块 | 取消 | 允许 | 允许 | H3e |
| 潜行 + 放方块 | 不影响 | 不影响 | 不影响 | — |

> 破坏保护是**方块级**而非整事件取消：只让商店箱免疫，旁边的方块照常被炸掉或被活塞推动。这一条**故意偏离 Java 版**（Java 一旦命中受保护方块就取消整场爆炸/推动），理由见 ADR-0004。

创建失败打掉牌子时（仅当 `removeInvalidShopSign = true`），按 Java 版 `breakNaturally` 语义**有掉落**（可配 `dropSignOnCreationFailure`，默认 true）。

### 9.3 判定"这个方块是否被商店占用"

- 方块本身是有效商店牌 → 是。
- 方块是容器 → 查它六个相邻面（以及双箱另一半的六个相邻面）是否存在有效商店牌，且该牌连到的容器正是它 → 是。
- 判定为纯读取，无缓存，失败即放行（不误伤普通箱子）。

---

## 10. 交易流程

### 10.1 交互

1. `PlayerInteractBlockEvent` 命中有效商店牌 → `event.setCancelled(true)`。
2. 弹 `SimpleForm`（标题：`<店主> 的商店`；管理员商店用 `管理员商店`）：
   - 正文直接列出店主、商品、每份件数、买入价、卖出价、可成交份数、余额（经济后端未接入时显示占位，不编造数字）
   - 买家视角：`购买（1 份 N 个 / B 价）`、`出售（1 份 N 个 / S 价）`；缺哪一边价格就没有哪个按钮
   - 店主视角（店主与 OP）：`管理商店`（改价 / 开箱 / 关店），**没有**买卖按钮（店主不能跟自己交易）
3. 选「购买」或「出售」→ 弹 `CustomForm`：
   - **份数**选择：`1 份 / 2 份 / 4 份 / 自定义份数`，上限 = `min(maxShopAmount, 可成交份数, 背包可容纳份数, 余额可承受份数)`
     （`maxShopAmount` 在这里当**单次份数**上限用，与 5.1 里「第 2 行的份量上限」是同一个键的两种限制——一个防超大份量，一个防超大单。写成 `min` 是 DESIGN 的原意，P0 不改键名。）
   - 卖出用同一张表单，只是四个上限换了角色：`min(maxShopAmount, 你手里的份数, 商店箱还能装的份数, 店主余额可承受份数)`（见 10.3）
   - 实时显示每份数量、总件数、总价、余额、库存
   - 总价 = `份价 × 份数`，恒为整数，不出现小数（对齐 Java 版默认语义，见 5.1）
   - 实际份数或金额与上面显示的不一致（库存变了、店主改了价）→ **不执行**，先弹一张确认表单把真实金额摆出来（见 10.3）
4. 提交 → 执行交易 → 消息反馈。

> **P0 现状（04 落地后）**：第 3 步的「实时显示」没做——`CustomForm` 正文在发出时就定死，
> 要实时重算得用 `sendUpdate` 反复重发整张表单。现在的做法是**逐档列出**总件数与总价、
> 自定义档给出乘数与上限；只要实际份数、件数或金额与玩家看到的对不上（商店箱里的货变了、店主改价、
> 一份的件数变了），就**不执行**，先弹一张确认表单把真实金额摆出来（见 10.3）。

> **P0 现状（05 落地后）**：买入与卖出共用同一张份数表单、同一个份数算法（`planTrade`）
> 与同一套确认表单，差别只在「谁的货、谁收货、谁付钱」与几个词（买入价/卖出价、扣款/收入）。
> 商店箱为空时**不能卖给它**：商品权威是容器第一个非空槽位（5.3），空了就没人知道这家店收什么。

> 牌上第 2 行是**一份**的件数，表单里选的是**份数**。这是 Java 版"右键一次成交一整份"的等价物；Java 的 shift 行为（按单价重算任意数量）不在 P0，因为它会引入小数价格。

### 10.2 执行顺序（预校验 + 快照 + 双向补偿）

物品与余额不在同一个事务里，**任何"先做 A 再做 B"都有中间态**。实现按以下顺序，并把补偿失败当成严重错误处理：

```
1. 解析商店（牌子 + 容器 + 商品 + 价格）            // 失败 → 提示无效商店
2. 预校验：余额、库存、背包空间、冷却、权限          // 失败 → 直接返回，无副作用
3. 计算可成交数量 n（部分成交开关）                 // n <= 0 → 提示缺货/缺钱/背包满
4. 快照：玩家背包与容器受影响槽位的 ItemStack 副本    // 只为补偿用
5. 金额操作：玩家 ↔ 店主（优先用后端的原子 transfer）
6. 物品操作：容器 ↔ 玩家
7. 任一步失败 → 按快照反向补偿（物品回写、金额反向 transfer）
8. 补偿失败 → 严重日志 + 通知 OP + 冻结该商店/该玩家本次操作，不再重试
9. 刷新：容器 refresh、玩家背包刷新
10. 消息 + （P1）流水
```

买入与卖出走的是**同一套流程**，只有两侧的角色反过来：

| 步骤 | 买入 | 卖出 |
| --- | --- | --- |
| 5 金额 | 买家 → 店主 | 店主 → 卖家（玩家） |
| 6 物品 | 商店箱 → 玩家背包 | 玩家背包 → 商店箱 |
| 8 冻结键 | 买家 XUID + 牌子坐标 | 卖家 XUID + 牌子坐标 |

原则：

- 第 2 步预校验覆盖 95% 的失败，尽量让第 5/6 步不会失败。
- 第 5 步放在物品搬运之前，因为金额接口的失败语义明确（`withdraw` 返回 false 且不变更），而物品搬运可能部分成功。
- 第 5 步开始之前先确认两边的账户都能记账（`get` 返回非空）：后端的 `transfer` 是
  「先扣款再入账，入账失败就把钱加回去」，而加回去本身也可能失败；读不到账户就根本不要开始动钱，
  这样两边余额一分未变。
- **买卖都先钱后货，不会产生复制窗口**，因为「货」始终沿着单方向移动并且**先扣后给**：
  买入是先从箱子取货再塞背包（中间那一刻货两处都没有），卖出是先从背包取货再放进箱子
  （同样两处都没有）。这是一段「丢失窗口」，退路是快照回写 + 退款；反过来先给后扣，
  中间那一刻才是同一件货同时存在于两处——那才是复制。
- 若后端不支持原子 `transfer`，用 `withdraw` + `deposit` 手动实现，并保证 `deposit` 失败时把 `withdraw` 的钱退回。
- 补偿逻辑必须在同一次事件处理内完成，不依赖重启恢复。

### 10.3 部分成交（对齐 `ALLOW_PARTIAL_TRANSACTIONS = true`）

- 买：`n = min(请求量, 库存, 背包容量, 余额可承受量)`。
- 卖：`n = min(请求量, 玩家持有量, 容器剩余空间, 店主余额可承受量)`。
  （店主余额是第五条限制：卖出是店主付钱，钱不够就只能少收一点或者整笔拒绝。）
- `n` 与请求量不同时，总价按 `单价 × n` 重算，并在确认表单里提前显示。
- 关闭部分成交时，`n < 请求量` 直接失败，并且**要说清是哪一条不够**：
  「箱子里的货不够 / 余额不够 / 箱子装不下」是三件事，不能一律报成「输入非法」。
  （`planTrade` 一份实现里就分好了「哪条上限挡住了」，只有文案按方向换说法。）

### 10.4 反刷保护

- 同一玩家 250ms 内重复提交 → 忽略（对齐 `SHOP_INTERACTION_INTERVAL`）。
- 店主不能在自己的商店交易（可配）。
- 创造模式玩家默认禁止交易（可配）。
- 表单提交是权威操作，不信任客户端传来的价格。

---

## 11. 权限模型

| 角色 | 判定 | 能力 |
| --- | --- | --- |
| 游客 | 其他玩家 | 买卖、查看商店信息 |
| 店主 | **正面记录的编辑者 XUID == 玩家 XUID**（ADR-0005） | 编辑牌面、染色、上蜡/除蜡、开箱、拆店、管理商店 |
| OP | `Player::isOperator()` | 全部，包括创建/管理管理员商店 |
| 管理员商店 | 第 1 行写 `adminShopName` **且创建者是 OP** | P0：库存来自容器；管理由 OP 负责 |

- **不解析玩家名**。店主身份直接来自告示牌正面的 `SignBlockActor::Text::mLastEditXuid`（引擎自己持久化的字段），与第 1 行的文本无关。第 1 行只是显示，规范化时被覆盖成该 XUID 的当前名字。
- 因此玩家改名不影响商店归属，也不需要 `NameCache` 或任何外部映射表。
- 代价：不能通过手写别人的名字来开店，也不能凭空转让商店（转让只能由对方来改这块牌子，而这个动作本身受店主权限约束）。
- 非 OP 在牌上写 `adminShopName` 时，这一行按普通玩家名处理 —— 由于第 1 行本来就只是显示，结果是"普通商店，店主是写牌的人"，不会被误判成管理员商店。
- 已有商店牌被非店主编辑时：交互阶段拦下（不弹 UI）+ 提交阶段兜底取消，见 9.2。

---

## 12. 实施计划

### P0：可玩闭环

0. 工程骨架：`levilamina 26.51.8`、iListenAttentively 依赖、`ss::shop` 命名空间、配置加载（**已完成**）。
1. `ShopSign`：四行解析、校验、规范化。**没有物品名解析**（商品来自容器，见 5.3）。
2. `ShopOwner`：读正面 `mLastEditXuid`，解析成 XUID + 当前显示名。
3. `ContainerFinder`：墙牌背面优先 + 六面顺序 + 双箱合并 + "一个容器只能被一个牌绑定"校验。
4. `ShopService`：创建/编辑/销毁、多商店规则、保护判定、上蜡状态查询。
5. H1：监听 `PlayerEditSignBeforeEvent` → 创建/编辑（改 `text()` 完成规范化）。
6. 交互与编辑拦截：`PlayerInteractBlockEvent` → 表单（`SimpleForm` + `CustomForm`），并拦下非店主的编辑/染色/墨囊/上蜡/除蜡。
7. 交易：预校验 + 快照 + 转账 + 搬物品 + 补偿 + 刷新；份数制计价（10.1）。
8. 保护：开箱、拆牌、拆箱、H2 漏斗、H3 方块级爆炸/活塞/凋灵/火焰。
9. 经济后端：`LegacyMoneyEconomy` + `ScoreboardEconomy` + `NullEconomy` + 探测与回退（第 8 节，**不是内存桩**）。
10. 手工验收（第 13 节）。

### P1

- 空店清理、创建费、税、退款
- 管理员商店的无容器模式、服务器账户
- shift 快捷成交（按单价重算任意数量，需引入小数价格）
- 交易流水 + 平均价格
- 管理员店无容器模式、服务器账户
- `ShopIndex` 索引与 `/shop` 管理命令

### P2

- 悬浮字（客户端模组或假实体）
- 掉落物展示
- GUI 美化、多语言

---

## 13. 验收场景（手工测试清单）

**创建**

1. 潜行把墙牌贴在箱子侧面 → 写 `任意名 / 64 / B 10 : S 5 / ?` → 第 1 行变成编辑者当前名字，第 4 行变成容器里第一个非空物品的展示名。
2. 潜行把站立牌立在箱子顶面 → 同样成为商店。
3. 站立牌立在箱子**旁边的地上** → 不被认作商店，提示"牌子要贴在或立在箱子上"。
4. 墙牌贴在箱子**旁边的墙上** → 不被认作商店，同样提示。
5. 牌子没有附着容器 → 提示并保留牌子。**管理员商店同样需要容器**。
6. 第 3 行没有 B/S → 视为普通告示牌，原样保留，不被改写。
7. 数量为 0 / 超过 3456 → 提示并保留牌子。
8. 同一个箱子再挂第二块牌（无论谁）→ 拒绝，提示"这个箱子已经有商店了"。
9. 双箱：两半各挂一块牌 → 第二块被拒（两半合并后是同一个库存）。
10. 双箱：牌子只贴在一半上 → 可交易全部 54 格。
11. 容器为空时写商店 → 提示"请先在箱子里放商品"，不创建。
12. 编辑已有商店为非法内容 → 整个包被丢弃，**旧商店保持原样**并提示原因（与 §4.2 一致；Java 版会让旧店失效，这里不做，宁可少一次误操作）。
13. 非 OP 在第 1 行写 `Admin Shop` → 仍是普通商店，店主是自己。

**牌面防护**

14. 非店主点商店牌 → 弹商店表单，**不弹编辑 UI**。
15. 非店主手持染料点牌 → 拒绝，颜色不变。
16. 非店主手持墨囊 / 荧光墨囊 → 拒绝。
17. 非店主手持蜜蜡 → 拒绝，牌子不上蜡。
18. 非店主手持斧头点已上蜡的商店牌 → 拒绝，除不了蜡。
19. 店主执行 14~18 → 全部允许。
20. 上蜡后的商店 → 店主自己也改不了，必须先除蜡；商店功能照常可交易。

**交易**

21. 买：库存与余额都够 → 钱货两清，库存减少、背包增加。
22. 买：余额不足 → 无任何变化。
23. 买：库存不足且允许部分成交 → 按实际可成交份数成交并提示。
24. 买：背包满 → 拒绝，无变化。
25. 卖：玩家没有该物品 → 拒绝。
26. 卖：店主资金不足 → 拒绝或部分成交。
27. 店主点自己的牌 → 进管理，没有买卖按钮。
28. 连续快速点牌 → 250ms 内只处理一次。
29. 表单选 4 份、牌上写 64 个 / B 10 → 总价 40，无小数。

**保护**

30. 非店主开商店箱 → 打不开。
31. 非店主拆商店箱 → 拆不掉。
32. 非店主拆商店牌 → 拆不掉。
33. 店主拆牌 → 成功，箱子恢复普通箱子。
34. 拆掉双箱一半 → 商店失效，牌子物理掉落。
35. TNT 在商店箱旁爆炸 → **商店箱幸存，旁边的方块照常被炸掉**。
36. 活塞推商店箱 → 推不动；推旁边的方块 → 正常推动。
37. 凋灵破坏 / 火焰 → 商店箱不受影响。
38. 漏斗方块在商店箱上方抽、下方塞 → 都无效。
39. 漏斗矿车停在商店箱下方 → 吸不走货（与漏斗方块共用同一拦截）。
40. 铜傀儡走到商店箱旁 → 取不走也放不进。
41. 投掷器朝商店箱喷物品 → 塞不进去，物品落到地上。
42. 非店主拆掉商店牌背后那块墙 → 拆不掉（牌子不会掉落）。

**边界**

43. 服务器重启 → 商店仍可用（牌子为源）。
44. 外部工具改牌子文本 → 下次点击按新文本解析。
45. 背面写商店格式 → 不生效。
46. 方块被破坏导致牌子失附着 → 商店消失。
47. 玩家改 Xbox 名 → 商店仍归他（XUID），第 1 行在下次规范化时更新。

---

## 14. 风险与待决

| 项 | 状态 | 应对 |
| --- | --- | --- |
| H1 告示牌编辑提交捕获 | **已验证可行** | iListenAttentively 的 `PlayerEditSignBeforeEvent` 已实现，NBT/时序/取消语义全部明确（见 6.1） |
| 牌子文本被改写的安全阀 | 设计约束 | `removeInvalidShopSign` 默认 false，解析失败只提示，不删牌 |
| 第三方 mod 依赖 | 已接受 | 未装 iListenAttentively 或版本不足时，`Shop.dll` 在加载阶段失败（ADR-0003），不做降级 |
| 补偿失败（金额或物品） | 设计约束 | 严重日志 + 通知 OP + 冻结该次操作，不自动重试 |
| 取消 `useItemOn` 能否阻止开箱/开牌 | 已绕开 | 开箱用 `PlayerOpenContainerBeforeEvent`；开牌用 `PlayerInteractBlockEvent` |
| 基岩版单行字符数 | 待实测 | 商品名可能被截断，全名在表单里显示；不影响交易（商品来自容器） |
| 双箱配对时机 | 待实测 | 配对失败按单箱处理 |
| 经济后端 | 接口已定，后端待实现 | LegacyMoney 优先，记分板兜底；离线店主按 8.5 处理 |
| LeviLamina 目标版本 | 已定 `26.51.8`（兼容 `>=26.51.7 <26.60.0`） | iListenAttentively 0.17.0、行为以实际头文件为准 |
| 商店税与 `pay_tax` 双重扣税 | 已知，**无法程序化检测** | `shopTaxRate > 0` 时无条件 WARN；服主不关 `pay_tax` 则商店税按 0 处理（见 8.9） |
| H2/H3 自定义 hook 的符号漂移 | 已知 | 每个 LeviLamina 大版本回归一次；符号在 26.51.8 头文件中已确认存在 |
| 编辑拦截的用户体验 | 待实测 | 交互阶段拦下可以不弹 UI，但需确认 `PlayerInteractBlockEvent` 的取消确实阻止 `OpenSignPacket` |
| 包路径是否写入 `mLastEditXuid` | 待实测 | `setMessageForServerScripingOnly` 需要显式传 `ownerID`；若包路径不写，03/06 需要在写入后补一次显式 ownerID |
| 侧挂的悬挂牌无法从状态判定墙面 | 已知，P0 不支持 | 基岩把两种悬挂形态合并成一个方块 ID，侧挂时 `facing_direction` 与墙面垂直；猜错会误绑旁边的箱子 |
| 上蜡状态读取 | 已确认字段 | `SignBlockActor::mIsWaxed`（bool），无 xuid 记录 |
| 并发/线程安全 | 待验证 | 表单回调切主线程执行方块操作 |

## 15. 配置草案

```json
{
  "adminShopName": "Admin Shop",
  "shopContainers": ["minecraft:chest", "minecraft:trapped_chest"],
  "allowMultipleShopsAtOneBlock": false,
  "allowOneContainerPerShop": true,
  "maxShopAmount": 3456,
  "allowPartialTransactions": true,
  "blockSellPriceHigherThanBuy": true,
  "allowOwnerOpenChest": true,
  "protectShopChest": true,
  "preventHopperExtraction": true,
  "shopInteractionCooldownMs": 250,
  "ignoreCreativeMode": true,
  "itemNameLocalized": true,
  "removeInvalidShopSign": false,
  "dropSignOnCreationFailure": true,
  "economy": {
    "backend": "auto",
    "scoreboardObjective": "money",
    "createObjectiveIfMissing": false,
    "requireOwnerOnline": true,
    "currencySymbol": "$"
  },
  "tax": {
    "shopTaxRate": 0.0,
    "taxDestination": "void"
  }
}
```

---

## 16. 附：关键 LeviLamina 接口索引

| 用途 | 头文件 |
| --- | --- |
| 告示牌方块实体 | `src/mc/world/level/block/actor/SignBlockActor.h` |
| 告示牌文本侧 | `src/mc/world/level/block/actor/SignTextSide.h` |
| 箱子方块实体 / 双箱配对 | `src/mc/world/level/block/actor/ChestBlockActor.h` |
| 容器读写 | `src/mc/world/Container.h` |
| 玩家背包 | `src/mc/world/actor/player/Inventory.h` |
| 方块实体类型 | `src/mc/world/level/block/actor/BlockActorType.h` |
| 方块实体刷新 | `src/mc/world/level/block/actor/BlockActor.h`（`refresh`） |
| 方块/方块实体读取 | `src/mc/world/level/BlockSource.h` |
| 告示牌编辑包 | `src/mc/network/packet/BlockActorDataPacketPayload.h` |
| 网络包 hook 样板 | `src/ll/api/event/player/PlayerChatEvent.cpp` |
| 玩家 ↔ 网络标识 | `src/mc/network/NetEventCallback.h`（`_getServerPlayer`） |
| 点击方块事件 | `src/ll/api/event/player/PlayerInteractBlockEvent.h` |
| 破坏方块事件 | `src/ll/api/event/player/PlayerDestroyBlockEvent.h` |
| 表单 | `src/ll/api/form/SimpleForm.h`、`CustomForm.h` |
| 记分板 | `src/mc/world/scores/Scoreboard.h`、`Objective.h`、`PlayerScoreSetFunction.h` |
| 玩家 UUID ↔ XUID | `src/ll/api/service/PlayerInfo.h` |
| 告示牌编辑事件（参考实现） | iListenAttentively `src/ila/event/world/actor/player/PlayerEditSignEvent.cpp` |
| 开箱 / 攻击方块 / 爆炸 / 活塞 / 凋灵 / 火焰 / 双箱 / 货币事件 | iListenAttentively `src/ila/event/**` |
| 日志 | `src/ll/api/io/Logger.h` |

---

## 17. 参考实现：ChestLock（`ChestLock`）

用户此前实现的通用箱子锁，LeviLamina 1.6.1、C++20、xmake，共 448 行。它验证了本模组需要的大部分保护路径，但**存储载体与商店场景不兼容**，因此定位为「hook 点参考」而非直接依赖。

### 17.1 可复用的部分

| 能力 | 实现位置 | 结论 |
| --- | --- | --- |
| 开箱拦截 | `Event.cpp` → `PlayerInteractBlockEvent` + `setCancelled(true)` | 已被实际使用，说明取消能阻止开箱；仍需在目标版本回归 |
| 拆箱拦截 | `Event.cpp` → `PlayerDestroyBlockEvent` + `setCancelled(true)` | 同上 |
| 漏斗抽/塞 | `Hook.cpp` → `Hopper::_tryPullInItemsFromAboveContainer` / `_pushOutItems` | 直接对应 H2 |
| 爆炸保护 | `Hook.cpp` → `BlockEventCoordinator::sendEvent`，从 `ExplosionStartedEvent::mBlocks` 里 `erase` | 对应 H3 |
| 凋灵保护 | `Hook.cpp` → `WitherBoss::_destroyBlocks`，先用 `fetchBlockEntities` 预检 | 对应 H3 |
| 活塞保护 | `Hook.cpp` → `PistonBlockActor::_attachedBlockWalker` | 对应 H3 |
| 双箱配对 | `Hook.cpp` → `ChestBlockActor::canPairWith` | 对应 H4，但原实现逻辑被注释，等于未完成 |

上述符号在 LeviLamina `main`（v26.51.6）上均已核对存在：
`src/mc/world/actor/Hopper.h`、`src/mc/world/events/BlockEventCoordinator.h`、
`src/mc/world/events/ExplosionStartedEvent.h`、`src/mc/world/actor/boss/WitherBoss.h`、
`src/mc/world/level/block/actor/PistonBlockActor.h`、`src/mc/world/level/block/actor/ChestBlockActor.h`。

### 17.2 不能直接复用的部分

1. **存储载体冲突**：ChestLock 把锁信息写进 `BlockActor` 的 CustomName，格式
   `Lock:<uuid> Perm:<int> Secret:<md5|NULL>`。商店的店主信息已经写在商店牌第 1 行，
   再往箱子上写一份会产生两个真相源；且 CustomName 是可见的容器标题，玩家会看到这一串原文。
2. **潜行直接放行**：`PlayerInteractBlockEvent` 回调开头 `if (Sneaking) return;`，
   意味着潜行右键可以绕过锁。商店交互不能用这个分支，必须改为「潜行 = 打开管理表单」或显式忽略。
3. **只覆盖 `BlockActorType::Chest`**：不含 `BarrelBlock`（`BlockActorType::BarrelBlock = 42`），
   商店若支持 barrel 需扩展判定。
4. **`md5(uuid + pwd)` 无盐**：作为游戏内密码强度够用，但重写时应换 `SHA256(uuid + ":" + pwd)`，
   并保留 `NULL` 表示无密码。
5. **版本差异**：`BlockActor::getCustomName()/setCustomName()` 在 1.6.1 上可直接用；
   在 `main` 上 `getCustomName()` 已移到 `VanillaBlockActor` / `IVanillaMainBlockActorComponent`
   （`src/mc/world/level/block/actor/VanillaBlockActor.h:121`），`setCustomName` 在 `Container`
   （`src/mc/world/Container.h:183`）。跨版本适配时以实际编译结果为准。
6. **双箱漏洞**：`ChestActorTryPairWithHook` 的逻辑全被注释，只保留 `origin`，
   所以「一半上锁、另一半未锁」可以绕过。商店侧必须按第 7.3 节把双箱当一个整体判定。

### 17.3 结论

- 商店箱保护**按第 9 节的「牌子推导」实现**，不写 CustomName，不与 ChestLock 抢字段。
- H2/H3/H4 的 hook 代码直接移植 ChestLock 的写法（去掉锁判定，换成「该容器是否被商店牌连接」）。
- 若后续需要「牌子被拆后箱子仍保留归属」，再引入独立存储（外部 JSON / SQLite 按维度+坐标），
  而不是复用 CustomName。

---

## 18. 决策记录

| 编号 | 决策 | 理由 / 备注 |
| --- | --- | --- |
| D1 | 在 `Shop` 目录新建实现，不迁移 `ChestShop` 旧工程 | 旧工程无有效实现（核心文件为空或全注释），且经济层不再复用 |
| D2 | 牌子为主交互，点击后弹表单 | 基岩版左/右键语义不可靠，且需数量选择；兼顾 Java 版手感与触屏可用性 |
| D3 | 经济后端留 `EconomyProvider` 契约，暂不实现具体后端 | 契约对齐 EconomyBridge 的形状；P0 先用记分板实现 + Null 实现 |
| D4 | 绑定规则完全照搬 Java 版 | 相邻容器、墙牌优先背面、同方块多牌仅限本人、双箱共享 |
| D5 | 生命周期完全照搬 Java 版：牌子即数据源 | 无商店注册表，拆牌即关店，重启无需迁移 |
| D6 | 先做「商店箱保护」再做交易细节 | 没有容器保护时经济模型不成立，属前置项 |
| D7 | 商店箱保护按「牌子推导」，不写 `CustomName` | 避免与 `ChestLock` 抢字段，也避免容器标题暴露锁串 |
| D8 | 拆牌即关店，不为箱子保留外部归属记录 | 与 D5 一致；防盗靠「非店主不能拆牌」而非事后追溯 |
| D9 | `ChestLock` 的潜行绕过、双箱配对缺失只记录不修改 | 现有代码先不动；正确实现写进 Shop 的 protect 模块 |
| D10 | 非法商店牌默认保留，`removeInvalidShopSign` 默认 false | H1（`BlockActorDataPacket`）未实测前，避免误删玩家告示牌 |
| D11 | P0 的管理员店必须有容器 | 不允许「凭空发货/收钱」的桩行为进生产；无容器模式等经济方案 |
| D12 | 交易采用预校验 + 快照 + 双向补偿 | 物品与金额不在同一事务；补偿失败按严重错误处理 |
| D13 | 参考 EconomyBridge 的接口形状，但不依赖它 | 不引入第三方静态库/许可证；后端自己实现（见第 8 节） |
| D14 | 后端不支持离线时，店主离线则禁止交易 | 记分板只能给在线玩家记账，避免钱货不对等（见 8.5） |
| D15 | 后端优先级：LegacyMoney > 记分板 > Null（`builtin` 留 P1） | 装了 LSE 的服必有 LegacyMoney；它支持离线、事务、税率（见 8.4） |
| D16 | 买卖必须走 `LLMoney_Trans`，不拆成 Reduce+Add | 事务原子 + 天然拒绝自买自卖（见 8.7） |
| D17 | `levilamina` 目标提到 26.51 | 对齐 LSE 0.22.3 / EconomyBridge；最终定为 26.51.7+（见 D22） |
| D18 | 商店自己收一道税，不依赖 `pay_tax` | 服主可单独配置商店税率（见 8.9） |
| D19 | 商店税默认销毁（`taxDestination = void`） | 不引入额外账户；需要税收账户时 P1 再加 |
| D20 | `shopTaxRate > 0` 时无条件 WARN，要求手动设 `pay_tax = 0` | LegacyMoney 无公开 getter，无法程序化检测；不关闭则商店税按 0 处理 |
| D21 | 事件层依赖 iListenAttentively（不照抄源码） | 官方维护、跟随 LL 升级；未安装则拒绝启用（见 6.4） |
| D22 | `levilamina` 目标 `26.51.7+` | 同时满足 iListenAttentively / LSE / EconomyBridge 的下限 |
| D23 | 数量语义为「份」：牌上第 2 行 = 一份的件数，牌上价格 = 一份的总价 | 对齐 Java 版默认语义；表单选份数，总价恒为整数。例外：价格行允许小数，向下取整后再参与计算（见 5.2），因此仍然不会出现非整数总价 |
| D24 | 只有 OP 能创建管理员商店 | 基岩版没有权限节点，OP 是唯一可用判据；非 OP 写 `Admin Shop` 按普通商店处理（见 11） |
| D25 | 破坏保护做到方块级，**故意偏离 Java 版** | Java 命中受保护方块就取消整场爆炸/推动；基岩服里整场哑火会被当 bug（见 ADR-0004、6.2） |
| D26 | 店主 = 正面记录的编辑者 XUID，第 1 行只做显示 | 玩家名可改，XUID 稳定；避免外部 name→XUID 映射表（见 ADR-0005） |
| D27 | 商品权威是容器第一个非空槽位，牌面第 4 行只做显示 | 基岩没有稳定的物品 ID 文本；避免本地化名反查与同名不同物（见 5.3） |
| D28 | 一块告示牌一个商店，只用正面；背面是布告板 | 规则一句话说完；避免"第 5 行在背面"的反直觉拼接（见 ADR-0006） |
| D29 | 编辑/染色/墨囊/上蜡/除蜡一律要求店主或 OP；上蜡不改变商店语义 | 引擎的上蜡是编辑冻结，不是权限；我们只把"除蜡"纳入店主权限（见 9.2） |
