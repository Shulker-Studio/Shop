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
| 牌面防护 | 非店主不能编辑、染色、上墨、上蜡（见 9.2） |
| 破坏防护 | 爆炸 / 活塞 / 凋灵 / 火焰不吞掉商店箱，旁边方块照常（方块级，见 6.2、ADR-0004） |
| 经济后端 | LegacyMoney / 记分板 / 空实现，自动探测（见 8.4） |
| 管理员商店（有容器） | 第一行 `Admin Shop` **且编辑者是 OP**；库存来自容器，资金侧是系统账户（买入销毁 / 卖出凭空记账）；无容器的无限库存模式放到 P1 |
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
| **店主（Owner）** | 商店牌正面所记录的编辑者 XUID（ADR-0005）；第 1 行只是他的显示名，`Admin Shop` 表示管理员商店 |
| **管理员商店** | 第 1 行为配置的 `adminShopName`（默认 `Admin Shop`）**且编辑者是 OP**。库存来自容器（7.4），资金侧是系统账户：不设上限 |
| **系统账户** | 管理员店的资金对手方，用**空账户键**（`Account{}`）表示。买入时钱被销毁，卖出时凭空记到玩家账上，没有真实余额可读（见 7.4 / 8.2） |
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
- **编辑、染色、荧光墨囊、墨囊、上蜡一律要求店主或 OP**；非店主在交互阶段就拦下了（不弹编辑 UI）。**文本**在提交阶段还有一道兜底；染色/上蜡只有交互阶段一道（引擎没有对应的提交事件可拦）。见 9.2。
- **上蜡不改变商店语义，但不可逆**。它是引擎级的编辑冻结，持久化在方块实体里，玩家可选；**原版去不掉告示牌的蜡**（斧头只对铜块有效，Mojira MC-261313 判 Works As Intended），所以「上蜡」也归店主权限——别人刷一下蜜蜡就等于把这块牌的文本永久冻住，店主只能拆牌重立（或将来的管理表单，P1）。
- 背面的任何书写都不影响商店，见 5.5。

---

## 5. 告示牌文本规范

### 5.1 四行

| 行 | 内容 | 权威性 | 规则 |
| --- | --- | --- | --- |
| 1 | 店主显示名 / `Admin Shop` | 显示 | 服务器规范化时用**归属店主 XUID 的当前名字**覆盖（新建商店时店主 = 编辑者；OP 代改不改归属，见 5.4 / 9.2）。写 `Admin Shop` 且编辑者是 OP → 管理员商店（见 7.4） |
| 2 | 份 | **权威** | 整数 `1 .. maxShopAmount`（默认 3456），表示一次交易的份量；牌上价格是**一份的总价**（对齐 Java 版默认语义）。**不支持** Java 的 `Q 64 : C 64` 计数格式 |
| 3 | 价格 | **权威** | `B <价> : S <价>`，B/S 至少一个；支持只写 `B <价>` 或 `S <价>`、`<价>`（视为 B）、`free`；末尾可带 `:x` 表示停业（5.2） |
| 4 | 商品显示名 | 显示 | 权威是绑定容器里**第一个非空槽位**的物品类型；规范化时用该物品的展示名覆盖。玩家写 `?`、留空或写别的名字都可以 |

即：**第 2、3 行是玩家说了算的，第 1、4 行是服务器说了算的**。

### 5.1.2 名字：过长与改名

**归属的真相源是 XUID（ADR-0005），第 1 行只是显示**。由此推出三条口径：

- **玩家改名不影响任何权限**：店主改 Xbox 名之后仍然是店主——能开箱、能改牌、能收款，“不能跟自己交易”的判据也不受影响。
- **牌面第 1 行会停在旧名**，直到有人（店主或 OP）重新编辑这块牌：编辑时第 1 行会按 XUID 查到的**当前名字**重写（`keepsPreviousOwner` 只保归属，不保旧文本）。
- **表单、提示、日志里一律是实时查到的名字**（`PlayerInfo::fromXuid`），所以改名后会看到“表里是新名、牌上是旧名”的不一致——这是故意的：牌子即数据源，不写盘、没有后台任务去扫牌改名。

**第 1 行不截断**，哪怕名字比一行宽。三个理由：① 管理员店的判据是“第 1 行整串等于 `adminShopName`”，截断会把判据变成前缀匹配；② XUID 查不到 `PlayerInfo` 时，第 1 行是显示名的退路（见 `ShopOwner`），截断会把这个退路弄碎；③ 牌面 NBT 里存全名、渲染交给客户端（能不能放下由客户端按行宽缩放或裁切），与第 4 行商品名同一个口径。真机看到的实际表现记在 12 的验收结果里。

### 5.1.1 什么才算「商店牌」（关键）

**普通告示牌绝不能被本模组改写或打掉。** 判定顺序与 Java 版一致：

1. 第 3 行去颜色码后必须含 `B` 或 `S`（大小写不敏感），否则**直接放行**，不做任何处理；
2. 第 1 行去颜色码后必须非空；
3. 满足 1、2 才进入第 5.2 节的完整校验链。

即：先判断「像不像商店」，再判断「是不是合法商店」。像但不是（数量非法、价格非法、没容器……）时，默认只提示原因并**保留原牌**；`removeInvalidShopSign` 默认 false，永远不主动删玩家的牌子。

> 第 1 行为 `Admin Shop` 的牌子仍受同样的文本规则约束，并且**同样需要容器**（D11）：P0 不允许凭空发货或收钱。

### 5.2 价格行文法（简化的 Java 版）

```
price_line  := segment (":" segment)? [ ":" closed ]
segment     := [B|S] amount | amount [B|S]
amount      := number | "free"
number      := digits [ "." digits ] [ "K" | "M" ]   // K=千, M=百万，可选
closed      := "x"                                    // 停业标记（大小写不敏感）
```

- **解析顺序是「先剥离最后一段」**：最后一段 trim 后等于 `x` 就判为停业并剔除，剩下的再按下面的规则解，且必须至少剩一个价。
  顺序不能反——单侧价格时停业段是**第二段**（`S 5 :x`），先查「段数 > 2」会把合法的 `B 10 : S 5 :x` 一起拒掉。
  只有 `x` 没有价（`x`、`:x`）按非法处理。
- 停业段大小写不敏感、`: x` 也认；**停业时**规范化恒输出末尾的 `:x`（`B 10 : S 5 :x`、单侧 `S 5 :x`），所以解析与格式化互逆（造不出「停业但两侧都没价」的牌面：那种定义在第 3 行只剩一个标记，解析与店面校验都拒）。
- 停业 ≠ 关店：价格一位不改、归属与库存不动、牌仍然「像商店牌」（第 3 行含 `B`/`S`），所以保护照旧；
  关店只有拆牌一种（归属与绑定一起没）。为什么会想「清空价格行」来停业——因为那样第 3 行没有 `B`/`S`，
  牌子整体失去保护，别人能改牌、能拆箱，比停业严重得多。
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

**P0 的实际做法**：包路径不保证写入 `mLastEditXuid`（见 6.5 第 3 条），所以在 `PlayerEditSignAfterEvent` 里**按 Before 阶段定下的归属写一次 ownerID、不改文本**：目标 XUID 与牌上记的不一致时，用当前文本调一次 `setMessageForServerScripingOnly(Front, 现有文本, 目标 XUID)`，不 `refresh`（客户端已经通过包拿到同一份文本）。普通牌、非法牌、附着容器不可用的牌一律不写。

**目标 XUID 怎么定**（同 06 修正）：归属规则是「牌子第一次成为商店时写给编辑者，之后改牌面不改归属」。两条都必要：

- 06 之后 OP 可以代改别人的牌面（§9.2）。若每次编辑都把 ownerID 换成编辑者，OP 改一下就把原店主锁在自己的店外——保护机制反过来成了“抢店”手段。
- `mLastEditXuid` 是**每一次**正面编辑都会变的字段，不只是“成为商店”那一次。只看“记的 XUID 是不是空的”会把“上一个人在这块牌上写过的普通公告”当成店主。所以「改之前是不是商店」这个事实必须从 Before 带到 After（`planSignEdit` 返回 `SignOwnerHandoff`，事件层暂存一条、After 时比对牌位与编辑者后取用）。
- 好处是它对「引擎到底写不写 `mLastEditXuid`」两种实测结果都成立：引擎不写，我们补上；引擎写了（包括写成了 OP），我们纠回来。

### 5.5 只看正面，背面是布告板

`SignTextSide::Front` 参与逻辑，`Back` 完全不参与：服务器不读、不写、不校验。玩家在背面写公告、批发价、店规都可以；即使在背面写出一套合法的商店格式，也不会被识别成第二个商店（ADR-0006）。

一块告示牌最多是一个商店。

> 背面的例外只限**文字**：空手或普通物品点背面，原版编辑界面照弹；但染色、墨囊、上蜡是**整块牌子**（或整个牌子实体）的事，不能靠站在背面绕过——刷个蜜蜡就把店主的编辑权限冻住了。所以这些操作的权限判定不看玩家站在哪一面（§9.2）。

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
| H2a | `Hopper::_tryPullInItemsFromAboveContainer(BlockSource&, Container&, Vec3 const&)`、`Hopper::_pushOutItems(BlockSource&, Container&, Vec3 const&, int)` | 漏斗**方块与漏斗矿车**从商店箱抽货 / 往商店箱塞货（两者共用 `Hopper` actor 的抽取逻辑，hook 一次覆盖两者）；命中商店箱就跳过这一次搬运 | 中（iListenAttentively 无漏斗事件；ChestLock 已验证写法） |
| H2c | `TransportItemsGoal::_executeEvent(TransportItemsGoal::Event) const`（`v1_21_100` 的新 AI goal，经 `ChestBlockActor::ChestCloser` 开箱取放） | 铜傀儡从商店箱取货 / 往商店箱放货：丢掉「即将开始的取 / 放」这一个事件，**不取消 goal**（`canUse` / `canContinueToUse` / `tick` 一个都不碰，所以不会因为 goal 被取消而失去运输行为） | 中（goal 级取消会影响该傀儡的所有运输、也可能误伤同样用这个 goal 的生物，所以不走那条路；代价是「这个事件是不是搬运闸门」只能真机确认，见下方落地口径与 §14） |
| H2d | `DropperBlockActor::pushOutItems(BlockSource&)` + `DropperBlockActor::getContainerAt(BlockSource&, Vec3 const&)` | 投掷器往商店箱塞货：只在这一次搬运的作用域里让容器查找交不出商店箱，原版因此把物品喷到地上，而不是把整个投掷器停掉 | 中（多压一层「`pushOutItems` 会先查目标容器」的假设；查不到就失效，`pushOutItems` 会留一条 warn 兜底） |
| H3a | `ExplosionStartedEvent::mBlocks`（`TypedStorage<8,64,std::unordered_set<BlockPos>>`），由 `BlockEventCoordinator::sendEvent` 拦截 | 爆炸时把商店方块从方块列表里**剔除**，而不是取消整场爆炸 | 中（同 ChestLock 已验证） |
| H3b | `PistonBlockActor::_attachedBlockWalker(BlockSource&, BlockPos const&, uchar, uchar)` | 活塞推动路径上遇到商店方块时返回 false（原版语义：这一路推不动）。推与拉都走这条移动路径，但「拉动也走它」尚未真机确认（12 有验收项） | 中 |
| H3c | `WitherBoss::_destroyBlocks(Level&, AABB const&, BlockSource&, int, WitherAttackType)` **只置标志后放行** + `Level::$destroyBlock(BlockSource&, BlockPos const&, bool, BlockChangeContext const&)` 在标志期内逐格拒绝 | 凋灵破坏范围内商店方块逐格留下，其余照常被毁——**不是**提前返回 | 中高（头文件只给整个 `AABB`，逐格剔除只能落在 `Level::destroyBlock` 上；依赖「凋灵的销毁走这条路径」，只能在真机确认） |
| H3d | `FireBlock::checkBurn(BlockSource&, BlockPos const&, int, IRandom&, int, BlockPos const&)` | 火焰烧到商店方块时跳过这一格，周围照常烧 | 中 |
| H3e | `PlayerDestroyBlockEvent` + 扫描周围附着的商店牌（对齐 Java `getAttachedSigns`） | **拆掉商店牌依附的方块**会让牌子物理掉落、商店消失，这是非店主绕过"不能拆牌"的真实路径。攻击路径（`PlayerAttackBlockBeforeEvent`）用同一个判据提前拦，免得非店主对着那面墙徒劳地挖 | 低（逻辑无需引擎内部符号，属于纯判定；与 07 共用同一个监听） |

> H3 之所以不用 iListenAttentively 的 `ExplosionBeforeEvent` / `PistonPushBeforeEvent` / `WitherDestroyBeforeEvent` / `FireTryBurnBlockBeforeEvent`，是因为那些事件只能**取消整场**爆炸/推动/燃烧，粒度不对（ADR-0004）。这几条 hook 的符号在 LeviLamina 26.51.8 头文件中均已确认存在，写法参照 ChestLock。
>
> **H3c 与 ChestLock 不一样**：ChestLock 发现范围内有受保护箱子就 `return`，等于取消整场凋灵破坏——那正是 ADR-0004 拒绝的行为（旁边方块也一块不掉）。26.51.8 的 `_destroyBlocks` 只拿到一个 `AABB`，没有逐格的插手点，所以这里改成「`_destroyBlocks` 只置一个『凋灵正在破坏』的线程标志后放行原版，由 `Level::$destroyBlock` 在标志期内逐格拒绝」。标志只在这一次凋灵破坏期间为真，因此玩家拆除（07）、爆炸（H3a）、我们主动打掉商店牌都不受影响。若真机发现凋灵的销毁不走 `Level::destroyBlock`（守卫一次都没命中），退路是回到提前返回，代价是凋灵范围内旁的方块不被破坏（12 有对应验收项）。

> **H2 的落点（09 落地时）**：四条路径（漏斗抽、漏斗/矿车塞、铜傀儡取放、投掷器塞）共用同一个判据——纯逻辑 `src/shop/ContentProtection.h` 的 `shouldSkipContentTransfer`（目标容器是商店箱就跳过这一次搬运），而「这一格是不是商店箱」仍然只问 `isShopBlockAt`。粒度的目标是「跳过这一次」，不是停掉装置：
>
> - **漏斗方块与漏斗矿车**：`_tryPullInItemsFromAboveContainer` 判漏斗正上方那一格（`pos` 是装置自己的位置，源容器固定在它上方），`_pushOutItems` 判 `attachedFace` 那一格（方向编号 0 下 / 1 上 / 2 北 / 3 南 / 4 西 / 5 东，由纯逻辑 `cellInDirection` 换成坐标，与 `neighboursOf` 共用同一张偏移表）。两条 hook 同时覆盖方块与矿车，不按形态各写一遍。
> - **投掷器**：`pushOutItems` 只开一个「这一次的目标是商店箱」的 thread-local 作用域，由内层的 `getContainerAt` 对这一格返回 `nullptr`；原版找不到容器就走「喷到地上」的分支，物品不会留在投掷器里。内层若一次都没被问过（符号漂移，或这条版本不经过它），`pushOutItems` 会记一条 warn——不许静默。
> - **铜傀儡**：只丢 `StartTakeInteractionWillSucceed` / `StartPlaceInteractionWillSucceed` 两个事件，「会失败」与「交互结束」原样放行；`canUse` / `canContinueToUse` / `tick` 一个都不碰，因此**不会因为 goal 被取消**而失去运输行为。这里有两件事只能在真机确认（12 有验收项），不能当成已证结论：① 这个事件是不是真正的搬运闸门（是的话货就搬不走，否则拦不住）；② 丢掉事件后 goal 会不会停在 `Interacting` 等一个永不开始的交互（表现为傀儡在箱子旁反复开箱或停住）。若 ① 不成立或 ② 发生，退路是在 `Interacting` 状态下让 `$canContinueToUse` 返回 false（引擎自己 `stop()` 收尾、关箱清状态，代价是这一趟运输白跑）。
> - 五条 hook 都要求「配置读不到就原样放行」（`currentShopConfig()`），与 H3 的兜底同一条。

### 6.3 直接监听即可（iListenAttentively 提供，无需自己 hook）

| 事件 | 用途 | 可取消 |
| --- | --- | --- |
| `ila::mc::world::actor::player::PlayerEditSignBeforeEvent` | 告示牌提交：创建/编辑商店、校验、规范化回写；**同时是提交阶段的兜底闸**（非店主 / 上蜡的牌一律不让这次编辑生效，见 9.2） | 是（把原文本写回去） |
| `ila::mc::world::actor::player::PlayerEditSignAfterEvent` | 牌子已更新后的通知：按 Before 阶段定下的归属写回 ownerID（DESIGN §5.4） | 否 |
| `ila::mc::world::actor::player::PlayerOpenContainerBeforeEvent` | 阻止非店主开商店箱；`pos` / `containerType` / `containerActorId` 齐全 | 是 |
| `ila::mc::world::actor::player::PlayerAttackBlockBeforeEvent` | 左键点方块（可做"左键=卖"，也用于提前拦截拆牌） | 是 |
| `ll::event::player::PlayerDestroyBlockEvent`（LL 原生） | 拆牌/拆箱保护；放行拆商店箱时先按原版语义打掉绑在上面的商店牌（双箱拆一半也要掉）；08 的 H3e（拆依附方块）也挂在这里 | 是 |
| `ila::mc::world::ChestPairWithBeforeEvent` | 双箱配对时校验两半归属 | 是 |
| `ila::mc::world::ExplosionBeforeEvent` | 备选：只能取消整场爆炸，粒度不对，08 改用 §6.2 H3a（`explosion()` 可取受影响方块） | 是（整场） |
| `ila::mc::world::PistonPushBeforeEvent` | 备选：只能取消整次推动，08 改用 §6.2 H3b | 是（整次） |
| `ila::mc::world::WitherDestroyBeforeEvent` | 备选：只能取消凋灵那一次整体破坏，08 改用 §6.2 H3c | 是（整次） |
| `ila::mc::world::FireTryBurnBlockBeforeEvent` | 备选：只能取消一次燃烧，08 改用 §6.2 H3d 的 `FireBlock::checkBurn` | 是（整次） |
| `ila::legacyMoney::MoneyChangeBeforeEvent` | 观测/拦截 LegacyMoney 的 Set/Add/Reduce/Trans | 是 |
| `ila::mc::server::ReceivePacketBeforeEvent<BlockActorDataPacket>` | 备选：若不想依赖 `PlayerEditSignBeforeEvent`，可自行过滤 | 是 |

### 6.4 依赖方式（已定：依赖 iListenAttentively）

- 编译期：`add_requires("ilistenattentively")` + `add_packages("ilistenattentively")`。xmake-repo 已收录该包（`packages/i/ilistenattentively/xmake.lua`），当前 `open` 最高版本为 **0.17.0**（要求 LL `26.51.*`），安装时会从源码编译并安装头文件 + 导入库。
- 运行时：`ILAPI` 展开为 `LL_SHARED_IMPORT`，即链接其 DLL 的导入表。**iListenAttentively 未安装时不是"功能缺失"，而是进程加载器直接失败**（Shop 整个 mod 起不来）。
- 因此必须在 `tooth.json` 声明运行时依赖 `github.com/LiteLDev/iListenAttentively`，并在文档/安装说明里写清楚。
- **还要在 `manifest.json` 的 `dependencies` 里写一行 `{"name": "iListenAttentively"}`**（与 LegacyScriptEngine 声明 `LegacyMoney` / `LegacyRemoteCall` 同一个机制）。
  这不是装饰：LeviLamina 按依赖图决定加载顺序，只把**正在加载的那个**模组的目录前插到 `PATH`。
  没有这行时按目录名字母序加载，`Shop`（`S`）排在 `iListenAttentively`（`i`）之前，
  `LoadLibrary(Shop.dll)` 解析导入表时 iListenAttentively 还没进进程 →
  `C++ Exception: std::system_error … [0x0000007E:system] The specified module could not be found.`（真机已复现）。
  依赖图里排在前面就会先加载它，之后同名 DLL 由加载器直接复用。
- **版本刻意不写**：`ll::mod::Dependency` 的 `version` 校验是「主版本相等且不低于声明值」，
  而本地/自编译的 iListenAttentively 的 `manifest.json` 常是占位版本（`0.0.0`），写死 `0.17.0` 会把这类安装直接判成
  「Missing dependency」，把一个能跑的组合挡在门外。版本下限交给 `tooth.json`（`>=0.17.0`）在安装期把关。
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

- 第 1 行等于 `adminShopName`（默认 `Admin Shop`）**且编辑者是 OP** → 管理员商店（非 OP 写同名只是普通店，见 11）。
- **P0 要求管理员店也必须有容器**，库存来自容器，不允许「凭空发货」的桩行为（D11）。
- 无容器的无限库存模式（Java 版 `AdminInventory`）放到 P1；P0 的库存与普通店走同一套容器统计。
- **资金端（P0 已落地）：系统账户**。管理员店不把牌上记的 XUID 当收款方（那只是当时创建它的 OP），
  买卖的对手方统一是「系统账户」——用**空账户键**表示，两侧动作是：
  - 玩家买入 → 钱被**销毁**（付给系统）；
  - 玩家卖出 → 钱**凭空记到玩家账上**（从系统付款）。
  这正好是 GLOSSARY 里「资金侧不设上限」的字面含义，也对应 §8.5 表格的「管理员店：系统账户」。
- 能力判据是后端声明的 `EconomyProvider::supportsSystemAccount()`：LegacyMoney（`LLMoney_Trans(to="")`
  与 `LLMoney_Add`）与记分板（`reduce` / `add` 在线玩家）都声明了它；声明不了的后端，管理员店
  **整家店不能交易**（点牌时买卖按钮一个不给，提交阶段同一条原因）。
- 不抽商店税：管理员店没有可抽的「店主收入」，再抽一道只是把买家的钱多烧一笔（`tradeTaxPlan` 判）。
- 补偿同理：买入失败要能把钱从系统退回玩家（凭空记账），卖出失败要能把钱从玩家退给系统（销毁）；
  所以声明这项能力就等于同时声明两个方向，不能只做一半。
- 管理权只给 OP（§11），但**买卖对所有人开放，包括 OP**：对手方是系统账户，不存在“跟自己交易”。
  表单角色由 `shopFormRoleOf(actor, adminShop)` 判，管理员店一律是访客。
- P1 剩下的是「服务器账户作为**真实**账户」：税收账户（§8.9 的 `taxDestination`）、无容器模式、
  以及给系统账户充值 / 查看余额的管理入口。

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
>
> 另外有两个能力协商方法，都是「能不能动系统账户（空账户键）」，但**用途不同、故意不合并**：
>
> * `supportsMoneySink()`（默认 `false`）：能不能把一笔钱付给系统。商店税的「销毁」就落在这上面（见 8.9）。
>   LegacyMoney 用 `LLMoney_Trans(from, "", n)`（从 `from` 扣掉、谁都不加）；记分板用「只 `reduce` 付款方、
>   谁都不加」，效果完全一样。**2026-11-01 之前记分板声明 `false`**（理由写的是「没有这种账户」），
>   站不住：税本来就不需要对手方，需要的只是「能不能单边扣钱」。现在两个后端都支持，服主换后端
>   不会突然开始或停止收税。
> * `supportsSystemAccount()`（默认 `false`）：能不能把系统账户当**交易对手**——两个方向都要会：
>   付给它（销毁）与从它付款（凭空记账）。管理员店的资金侧靠它（见 7.4）。LegacyMoney 两个方向都能
>   （`Trans(to="")` / `LLMoney_Add`），记分板靠 `reduce` / `add` 也能（只能动在线玩家，而交易的那一头
>   本来就在线）。
>
> 它们仍然是两项：一项是商店税的政策，一项是管理员店的资金端，将来加的后端可能只会其中一件。

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
- 服务器账户：P0 的落点是**空账户键的系统账户**（不是 P1 的「全 0 UUID 真实账户」）：
  后端用 `supportsSystemAccount()` 声明能不能把系统账户当对手方。管理员店的资金侧用它（7.4），
  税用的是另一个能力 `supportsMoneySink()`（8.9）——两者仍然分开：它们是两件不同的事（一项是商店税政策，
  一项是管理员店的资金端），将来加的后端完全可能只会其中一件。

### 8.3 参考实现与取舍

| 能力 | EconomyBridge | LegacyMoney | 本模组 |
| --- | --- | --- | --- |
| 接口 | `get/set/add/reduce/transfer` | C 导出 `LLMoney_*`（XUID 为键） | 同 EconomyBridge 形状（UUID 为键） |
| 存储 | 无（薄适配） | SQLite `economy.db`，`money` + `mtrans` | 由后端决定 |
| 离线玩家 | 两个后端都支持 | **支持**（LegacyMoney 按 XUID 记账；记分板按「XUID → 名字 → 身份」写分） | 由 `supportsOffline()` 声明 |
| 原子转账 | 自己补偿 | **SQLite 事务**（begin/commit/rollback） | 交给后端，接口只保证"失败不变更" |
| 税率 | 无 | `pay_tax`（转账时扣） | 不重复收税（见 8.7） |
| 事件 | 无 | `LLMoney_ListenBeforeEvent/AfterEvent`（可拦截） | 不依赖，但需容忍被别人拦截 |
| 销毁（税） | 无 | `LLMoney_Trans(from, "", n)` | `supportsMoneySink()` 声明；不支持就不发那一笔 |

借鉴的是接口形状与调用方式，**不复制源码**（EconomyBridge 为 LGPL-3.0，LegacyMoney 的许可证需按其仓库为准；本工程 AGPL-3.0）。

### 8.4 后端选择（**没有自动降级**）

> **P0 现状（10 落地后，2026-11-01 改过一轮）**：`createObjectiveIfMissing` 与
> `requireOwnerOnline` 都已生效，`/shop status` 会打出「配置值 → 实际选用值」。
>
> **`auto` 已被删除。** 它原来表示「LegacyMoney 探不到就用记分板」，默认值又恰好是它。
> 后果是服主的 LegacyMoney 一旦因版本不兼容没加载（导出缺失），本模组会**默默换一套账本**：
> 玩家余额一夜之间变成记分板上的 0，而日志里只有一条容易被忽略的 info。这与「配置非法就
> 拒绝加载」的既有原则自相矛盾——账本换家比配置写错严重得多。现在：
>
> - 合法取值只有 `legacy_money` / `scoreboard` / `null`（`builtin` 是 P1，写它会拒绝加载）；
>   **默认 `legacy_money`**。老配置里写 `auto` 会拒绝加载，并给出专门的迁移提示
>   （不是「拼写错误」那种泛泛的列表）。
> - **一个后端只探它自己**：写 `legacy_money` 就不看记分板，写 `scoreboard` 就不看 LegacyMoney。
>   探不到 → `NullEconomy`（余额读不到、交易全失败）+ **error 日志**说明探不到的原因、
>   以及「改成另一个值就能用」的具体建议。`selectBackend`（`src/shop/Economy.cpp`）里
>   没有任何跨后端的分支，探测计划同理（`probesFor` 只返回配置写着的那一个）。
> - 日志形如 `配置 legacy_money → 选用 legacy_money（支持离线店主，每笔交易一次原子转账）`；
>   探不到时 `配置 legacy_money → 选用 null（跳过 legacy_money：进程里没有 LegacyMoney.dll）`
>   再加一句「本模组不会自动改用另一个后端（账本换家必须由你在 `economy.backend` 里决定）」。
> - 探测结果**会缓存，包括「一个后端都没有」**：否则每次动钱都要重探、重刷日志。
>   装好 LegacyMoney 或建好记分板目标之后需要重启（或重载模组）才会重探。
> - 探测时机：`Shop::enable()` 时世界往往还没起来，所以挂 `ServerStartedEvent` **与
>   `PlayerJoinEvent`** 两个监听（真机首测发现前者在本服上没触发，见 §19.3），首次真正要用钱时
>   还有兜底。
> - 表单里仍只显示余额，不显示后端名；要看后端用 `/shop status`（§19.1）。

1. 配置文件显式指定 `economy.backend`：`legacy_money` / `scoreboard` / `null`（`builtin` 是 P1）。
2. `legacy_money` 的探测：`GetModuleHandle(L"LegacyMoney.dll")` 非空且 `LLMoney_*` 导出符号齐全
   → `LegacyMoneyEconomy`（**默认值**，支持离线、事务、税率、事件）。
3. `scoreboard` 的探测：记分板目标存在（或按 `createObjectiveIfMissing` 用 `dummy` 判据建出来）
   → `ScoreboardEconomy`（仅在线，且不能收税）。
4. `null` 直接就是空实现（余额恒为 0，交易全失败；只给开发用）。
5. `builtin`（自建 SQLite 账本）作为 P1，给不装 LegacyMoney 的服务器用；接口相同，`supportsOffline()` 返回 true。
6. **探测失败不 panic，但也不换账本**：落到 `NullEconomy` 并把原因与「怎么改配置」写进 error 日志。

> 生态事实：`LegacyMoney` 是 **LegacyScriptEngine 的硬前置**——LSE 的 `tooth.json` 把它声明成依赖，
> 缺它 LSE 根本起不来，所以**装了 LSE 的服务器必然有 LegacyMoney.dll**。这也是把默认值定为
> `legacy_money` 的理由：绝大多数生产服都跑 LSE，`legacy_money` 开箱即用；`scoreboard`
> 是给「不跑 LSE、只用原版记分板经济」的服务器的显式选项。

> **两档后端的差异（选之前要知道）**：
>
> | | `legacy_money` | `scoreboard` |
> | --- | --- | --- |
> | 余额存在哪 | `plugins/LegacyMoney/economy.db`（SQLite，`money` + `mtrans`） | 世界的记分板目标（默认 `money`） |
> | 离线店主 | 支持（余额按 XUID 记，不看在线） | 支持（**有身份**的离线玩家：借玩家表把 XUID 换成名字再写分） |
> | 金额范围 | `int64` | `int32`（超范围拒绝，不截断） |
> | 商店税 | 支持（钱付给空账户 = 销毁） | 支持（只扣付款方、谁都不加，同样是销毁） |
> | 管理员店 | 支持（`LLMoney_Add` / `Trans(to="")`） | 支持（`add` / `reduce` 在线玩家） |
> | 换档成本 | 余额不互通，两边各记各的 | 同左 |

### 8.5 离线店主策略

判断依据是后端的 `supportsOffline()`，不是"店主此刻是否在线"。

| 情况 | 支持离线的后端（LegacyMoney / Scoreboard / builtin） | 不支持离线的后端（留给将来的新后端） |
| --- | --- | --- |
| 买入（钱付给店主） | 正常成交 | **禁止成交**，提示「店主不在线」 |
| 卖出（店主付钱给买家） | 正常成交 | **禁止成交**，提示「店主不在线」 |
| 管理员店 | 系统账户，不受影响 | 同左 |

- 管理员店那一行不靠 `supportsOffline()`：它的两头是本应与“谁在线”无关，但记分板的系统账户只能动
  **在线玩家**——而交易的那一头（买/卖的玩家）本来就在线，所以两个后端都能成交（见 7.4）。
  真正会拦住管理员店的是 `supportsSystemAccount()`：声明不了就整家店不能交易。
- `Scoreboard` 后端下，点击牌子时就提前判定并禁用表单按钮，避免玩家填完数量才被拒。
- **余额读不到**（`get` 返回空，见 8.2）与「余额为 0」不同：点牌时正文写占位、不编造数字；
  真正要动钱的那一步（`moveMoney`）之前会再确认一次两边账户，读不到就整笔拒绝、两边余额未动，
  而不是「先扣款再退款」（§10.2 原则里的「第 5 步开始之前」）。
  不再在点牌阶段因此禁用按钮：读不到的只是账户，与这家店能不能收钱无关。
  管理员店的系统账户不是可读账户：这一道检查会跳过空的那一头（能力已在预校验里判过）。
- 配置 `economy.requireOwnerOnline` 只影响「后端不支持离线」这一支；对支持离线的后端无意义，对管理员店也无意义。
  （2026-11-01 起 `scoreboard` 也支持离线，这个开关在当前两个后端上都已是空转，保留是给将来新后端。）

> **记分板离线记账怎么落地（2026-11-01）**：记分板身份的内部键是 `PlayerScoreboardId`
> （= 玩家的 `ActorUniqueID`），而这个 ID **存在玩家自己的存档里**，所以人不在线也拿得到。
> 照着 LSE `mc.addPlayerScore(uuid, 目标, 值)`（「可修改离线玩家计分板」）的实现
> （`LegacyScriptEngine/src/legacy/api/PlayerAPI.cpp`）走一遍：
>
> ```
> XUID → 玩家表拿 uuid → DBStorage 的 player_<uuid> → 它的 ServerId → 存档里的 UniqueID
>      → scoreboard.getId(PlayerScoreboardId(uniqueId)) → modifyPlayerScore
> ```
>
> **用 ID 而不是名字**（一度想用「XUID → 名字 → 身份」，被这条实现否掉）：改名、重名都不影响，
> 也不会把分数记到同名的假玩家身份上。
>
> 任何一步拿不到就**安全失败**（`get` 返回空 = 读不到余额，交易被拒，绝不猜）：玩家表里没有这个 XUID
> （从没登录过本服）、存档里没有 `player_<uuid>` / `ServerId` / `UniqueID`、
> 或者 `getId` 返回 `INVALID`（这个人在本世界还没有过记分板身份）。
>
> **已真机验证（2026-11-01）**：店主完全下线时买家照常成交，钱写进店主的分值。

### 8.6 P0 的实现清单

- [x] `ScoreboardEconomy`：`Scoreboard::modifyPlayerScore` + `PlayerScoreSetFunction`，与 EconomyBridge 同路数；**写入前检查 `INT32` 范围**（`economy::toScoreValue`，超范围拒绝、不截断）。目标缺失时按 `createObjectiveIfMissing` 决定是否用 `dummy` 判据建一个。
- [x] `LegacyMoneyEconomy`：`GetModuleHandleW` + `GetProcAddress` 解析 `LLMoney_Get/Set/Add/Reduce/Trans`；**键直接用 XUID**（`PlayerInfo::fromUuid` 那一步在本代码库里不需要，ADR-0005 的店主身份本来就是 XUID）。
- [x] `NullEconomy`：全成功、不记账，仅开发；`available()` 为 false，交易一律拒绝而不是白送。
- [x] `EconomyManager`：探测、降级、日志（`/shop economy` 状态输出留到 P1 的命令层）。
- [ ] `builtin` 与流水/均价留到 P1。

### 8.7 LegacyMoney 对接细节（实现时必须遵守）

1. **键是 XUID 不是 UUID**：本代码库的账户键（`economy::Account`）本来就是 XUID，不需要 `PlayerInfo::fromUuid`（ADR-0005 的店主身份也是 XUID）。空 XUID 视为「无法记账」。
2. **必须用 `LLMoney_Trans` 完成买卖**，不要用 `Reduce` + `Add` 两条独立调用：
   - `LLMoney_Trans` 内部是 SQLite 事务，天然原子；
   - `LLMoney_Trans` 会应用 `pay_tax`，自己拼两条调用会漏税或重复扣税；
   - `Reduce`/`Add` 本身也是 `Trans` 的包装，`from == to` 会被拒（阻止店主自买自卖）。
3. **`LLMoney_Get` 有写副作用**：查询不存在的 XUID 会插入 `def_money` 记录并返回它。所以 `get()` 不能当作纯读，也不能把「新账户」理解成「读不到」；它的失败通道只有一条：**空 XUID 或数据库异常时返回 `-1`**（见其 `src/API.cpp`），本模组据此返回「读不到」。
4. **`val == 0` 是合法的**：`LLMoney_Trans` 会照常提交一条 0 元流水。所以「税为 0」必须由调用方跳过，而不是靠后端忽略。
5. **`allowNegative` 无法支持**：LegacyMoney 永远拒绝负余额，`allowNegative = true` 时按 `false` 处理并记 debug 日志。商店不使用该参数。
6. **可能被第三方拦截**：`LLMoney_ListenBeforeEvent` 允许其他插件取消转账，`LLMoney_Trans` 会返回 `false`。商店必须把 `false` 当作正常失败路径（提示"交易被服务器阻止"，无副作用）。
7. **货币符号拿不到**：LegacyMoney 的 `currency_symbol` 在它自己的配置文件里，跨模组读不到。`getSymbol()` 用 Shop 自己的配置项（默认 `$`），与 LegacyMoney 配置保持一致由服主负责。
8. **金额是 `long long`**，无 int32 问题；但 `Scoreboard` 后端有，`EconomyManager` 要按后端分别限制上限。
9. **`pay_tax` 会被叠加且无法读取**：`LLMoney_Trans` 只在**两边都非空**时给收款方加 `val - val * pay_tax`（`from` 或 `to` 为空时按全额），而 LegacyMoney 没有导出该配置的 getter。商店税与 `pay_tax` 同时开启会让店主被二次抽税、补偿金额漂移（反向 `Trans` 到账的也是 `val - val*pay_tax`）。P0 规则：`shopTaxRate > 0` 时**无条件 WARN** 要求服主手动设 `pay_tax = 0`；读不到就不猜，**不**自动禁用商店税（见 8.9）。
10. **绝不链接 LegacyMoney、绝不 include 它的 `LLMoney.h`**：那个头里全是 `__declspec(dllimport)`，一引用就会在 `Shop.dll` 里留下导入项，没装 LegacyMoney 的服务器连加载都会失败——「可选后端」当场作废。只能用 `GetModuleHandleW(L"LegacyMoney.dll")` + `GetProcAddress`，函数指针 typedef 自己写。构建后应确认 `Shop.dll` 的导入表里没有 `LegacyMoney.dll`。
11. **导出名不匹配就降级，不猜**：x64 的 C 导出是未修饰名（`LLMoney_Get` 这一组，已对照 `LegacyMoney.dll` 的导出表）；另一套 API 是 C++ 命名空间版（`LegacyMoney::LLMoney_Get`，返回 `ll::Expected`，mangled 符号）。`GetProcAddress` 拿不到未修饰名时判为「版本不兼容」（`ProbeIssue::ExportsMissing`）并降级，**不去猜 mangled 名字**。

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
| 管理员店 | **不收**：对手方是系统账户（7.4），没有「店主收入」这一侧可抽；`tradeTaxPlan` 对管理员店直接返回 0 |
| 舍入 | 向下取整到整数，余数留给店主，不出现负税 |
| 税为 0 | 完全跳过第二笔转账，不产生额外流水 |

**实现**（`src/mod/TradeService.cpp` 的 `moveMoney` + `chargeTax`；`T` = 总额，`X` = 税）：

```
买入：
1. Trans(buyer, owner, T - X)   // 主交易：店主实收 T - X，买家稍后还要付 X
2. 物品：商店箱 → 背包
3. Trans(buyer, "", X)          // 税：to 为空 = 系统回收（X == 0 时整步跳过）
卖出：
1. Trans(owner, seller, T)      // 主交易：玩家收到的就是牌面金额
2. 物品：背包 → 商店箱
3. Trans(owner, "", X)          // 税由店主另付：店主总共付出 T + X
4. 任一步失败 → 按已移动的金额反向补偿；补偿再失败才升级为 Broken（见 10.2）
```

**为什么收税排在物品之后**（与本节早先版本的字面顺序不同，10 落地时的决定）：

- 税一旦销毁就补不回来。若先收税、之后移物品那一步失败，退款只能退 `T - X`，
  买家凭空少一笔税，§10.2 的「任一步失败两边余额不变」当场不成立。
- 把收税挪到最后，任何失败点都只涉及已经移动过的金额（买入 `T - X`、卖出 `T`），
  补偿永远是精确的；补偿本身失败才升级为 `Broken`。
- 代价只有一个：物品已成交、税还没收到的那一小段时间里崩服，丢的是税而不是玩家的钱。
- 后端**不能单边扣钱**时（`EconomyProvider::supportsMoneySink()` 为 false），**整笔税都不算**：
  调用方直接把税率当 0，并打一条启动 warn。硬算下去只会让店主少收、买家少付，而没有任何地方记着
  这笔税，账对不上也查不到。当前两个后端都能单边扣钱（LegacyMoney 的 `Trans(to="")`、记分板的
  「只 `reduce` 付款方」），所以这道判据留着是给将来新后端的。
- 卖出方向的份数上限按**含税成本**算（`affordableUnitsWithTax`），否则会多卖一份、到收税那一步才失败。

**前提约束（P0 硬性要求）**：启用 `shopTaxRate > 0` 的前提是服主**手动把 LegacyMoney 的 `pay_tax` 设为 0**。

- LegacyMoney 没有导出 `pay_tax` 的 getter，Shop **无法**在运行时读取或检测这个值。
- 如果 `pay_tax > 0`，上面两笔 `Trans` 都会在收款方再抽一次：店主被抽两次（`total - tax` 再打折），且补偿金额会漂移（实际到账 ≠ 记账金额）。
- 因此启动时只要 `shopTaxRate > 0` 就**无条件打 WARN**，文案明确要求确认 `pay_tax = 0`；README 与安装说明同步写这一条。
- 若服主不关闭 `pay_tax`，P0 **不**自动禁用商店税（读不到就不猜，只打 WARN）：结果是店主被抽两次、补偿金额与实际到账不一致。前提由服主承担，风险登记在 §14。
- 两笔 `Trans` **不是跨调用事务**：第二笔失败时第一笔已生效，必须按 `total - shopTax` 精确补偿；补偿本身也可能失败，按第 10.2 节升级为严重错误。

---

## 9. 商店箱保护（P0 核心）

### 9.1 保护对象

被至少一块商店牌（正面**像**商店牌，判据见 §9.3）连接的容器（含双箱两半）以及这些商店牌本身。

### 9.2 规则

| 行为 | 非店主 | 店主 | OP | 实现依据 |
| --- | --- | --- | --- | --- |
| 右键点商店牌 | 打开交易表单（取消原版编辑） | 打开店主表单 | 同店主 | `PlayerInteractBlockEvent` |
| 潜行 + 右键点商店牌 | 打开交易表单（潜行**不是**后门） | 进原版编辑界面（上蜡时拦下并说清只能拆牌重立） | 同店主 | 同上；判定在 `src/shop/SignProtection.*` |
| 编辑商店牌文本 | **取消**（交互阶段就不弹 UI） | 允许 | 允许 | `PlayerInteractBlockEvent` + `PlayerEditSignBeforeEvent` 兜底（非店主提交也取消，见下） |
| 染色 / 荧光墨囊 / 墨囊 | **取消** + 提示 | 允许（交回原版处理） | 允许 | `PlayerInteractBlockEvent`（对照 `SignBlock::SignInteractionResult::UseDye` 等分支） |
| 上蜡 | **取消** + 提示 | 允许（交回原版处理） | 允许 | 同上（`UseHoneyComb`） |
| 右键点商店箱 | 取消（不能开箱） | 允许（可配 `allowOwnerOpenChest`，默认 true） | 允许 | `PlayerOpenContainerBeforeEvent` |
| 拆商店牌 | 取消 | 允许（拆除即关店） | 允许 | `PlayerAttackBlockBeforeEvent` + `PlayerDestroyBlockEvent` |
| 拆商店箱 | 取消 | 允许（连带关店） | 允许 | 同上 |
| 爆炸 / 活塞 / 凋灵 / 火焰 | 保护 | 保护 | 保护 | **方块级**：`ExplosionStartedEvent::mBlocks` 剔除、`PistonBlockActor::_attachedBlockWalker` 返回 false、凋灵破坏期间 `Level::$destroyBlock` 逐格拒绝、`FireBlock::checkBurn` 跳过这一格（见 6.2、ADR-0004） |
| 漏斗方块 / 漏斗矿车抽塞 | 阻止 | 阻止 | 阻止 | H2a（见 6.2） |
| 铜傀儡取放货 | 阻止 | 阻止 | 阻止 | H2c |
| 投掷器塞货 | 阻止 | 阻止 | 阻止 | H2d |
| 拆商店牌依附的方块 | 取消 | 允许 | 允许 | H3e |
| 潜行 + 放方块 | 不影响 | 不影响 | 不影响 | — |

> 破坏保护是**方块级**而非整事件取消：只让商店箱免疫，旁边的方块照常被炸掉或被活塞推动。这一条**故意偏离 Java 版**（Java 一旦命中受保护方块就取消整场爆炸/推动），理由见 ADR-0004。

**开箱与拆除**（表格里那三行）的落地口径（07）：

- 开箱只看配置 `allowOwnerOpenChest`（默认 true）：店主可关，OP 恒可；关掉时连店主也只能找 OP，但**不影响拆箱**——拆掉箱子本来就是店主的权利。
- 拆箱与拆牌都是「店主与 OP」。拆牌即关店；拆箱连带关店。
- 拆商店箱（双箱任意一半都算）时，先按原版语义把绑在上面的商店牌打掉（有掉落），商店随之失效。拆的若是**不**挨着牌子的那一半，另一半还在、牌子不会因失去支撑而自己掉落，这一步必须显式做（§13 第 34 条）。
- 普通箱子、普通告示牌不在判定内：开箱、攻击、拆除都交给原版。

**破坏防护**（表格里那两行：爆炸/活塞/凋灵/火焰、拆依附方块）的落地口径（08）：

- 保护对象与 07 完全一致：方块本身是商店牌（正面文本像商店牌）或绑着商店牌的容器（§9.3），不看 `shopContainers` 白名单。
- 粒度是**方块**，不是事件：爆炸只把商店方块从 `mBlocks` 里删掉（旁边的方块照炸），火焰只跳过被烧的那一格（旁边的木头照烧）；活塞没有「部分推动」语义，所以一次推动里包含商店方块时整次推动失败，但别处的活塞照常工作。
- **凋灵不提前返回**：`WitherBoss::_destroyBlocks` 只置「凋灵正在破坏」的线程标志后放行原版，由 `Level::$destroyBlock` 在标志期内逐格拒绝（§6.2 H3c）。提前返回会把整场破坏取消，违反 ADR-0004。
- 「拆商店牌依附的方块」单列一支（H3e）：拆的往往不是容器而是块普通方块，但牌子会物理掉落、商店随之消失，所以放行条件与拆牌相同（店主与 OP），提示要把后果说出来。牌挂在石头墙上也算——牌面判据不看附着容器（§9.2）。店主拆掉时什么都不用做：牌子由原版物理打落，与拆牌等价（§4.2）。

**内容防偷**（表格里那三行：漏斗方块/漏斗矿车抽塞、铜傀儡取放、投掷器塞）的落地口径（09）：

- 判据与 07／08 完全一致：只看「这一格是不是商店箱」（`isShopBlockAt`：牌面「像商店」+ 附着容器），不看 `shopContainers` 白名单、也不看店主——四条搬运路径（漏斗方块、漏斗矿车、铜傀儡、投掷器）对非店主、店主、OP 一律阻止。商店箱的内容只能通过交易流动，店主也不能拿漏斗替自己开箱。
- 粒度是「这一次搬运」：漏斗与投掷器跳过这一次、下一次照常搬运别处；铜傀儡只丢「即将开始取/放」的那一个事件，不取消 goal（见 §6.2 的落地口径）。
- 被拦之后物品去哪：投掷器走原版「找不到目标容器」的分支，物品**喷到地上**；漏斗与铜傀儡的物品留在原处，不复制、不掉落。
- 配置缺失时一律放行（与 08 同一条兜底）：模组没启用时这几条 hook 对普通容器毫无影响。
- 铜傀儡这一条的拦截只认 goal 记下的目标容器坐标（`mTargetContainerPosition`），粒度与可靠性都不如漏斗那两条；它是否真的挡住、会不会让 goal 卡在交互中，都以 12 的验收结果为准（§14 有两行对应风险）。

「编辑商店牌文本」有两道闸：交互阶段取消 `PlayerInteractBlockEvent`（非店主连编辑界面都弹不出来），提交阶段在 `PlayerEditSignBeforeEvent` 里再判一次（非店主提交不让生效）。后者是防绕过：别的模组、取消语义失效、以后从管理表单进编辑，都不该让改牌生效。

> **提交阶段不 cancel，改成「把原文本写回去」（2026-11-01 真机修正）**。用 `event.cancel()` 拦下时，
> 客户端那次编辑没有正常收尾：之后对这块牌子右键**再也不弹原版编辑界面**，必须退出重进才行
> （「新牌子被判非法后显示空白」与「改完还是原样」两种都中，后者与是否被拦无关的程度被真机确认）。
> 现在的做法是把 `event.text()` 设成**改之前那几行**，让原版完整走完一次编辑——玩家看到的还是原内容，
> 客户端也不会留下半截状态。副作用只有一个：写文本会让引擎把 `mLastEditXuid` 记成这次编辑者
> （§6.5 第 3 条），所以改之前是商店牌时必须像规范化那次一样**把归属保住**（同样的
> `SignOwnerHandoff`，`previousOwnerXuid` 填改之前的店主），否则 OP 代改一次非法文本就把店抢走了。
>
> 这条替代了更早的「cancel + 重发方块实体」写法：那个写法能让牌面立刻显示正确，
> 但会留下打不开编辑界面的问题，得不偿失。

有一个例外：**牌还在、但店停摆了**（附着格不再是可用容器，或牌面已经不严格合法）。这时没有表单可弹：能进原版编辑界面的判据与提交阶段相同（`mayEditShopSignText`），其余人被拦下并被告知停摆的原因（「商店箱不在了」/「写法现在不算有效商店」）——不能把编辑界面递给非店主，哪怕提交阶段还会兜底。

管理员商店不吃「店主」这一套：**只有 OP 算管理身份**（§11）。牌上记的 XUID 只是当时创建它的那个 OP，他被撤权之后不该继续拿到管理入口或改牌权限。

提交阶段的判据有三个特殊之处：

- **只看文本，不看容器**：判据是「改之前的正面文本像不像商店牌」+ 牌子上记的 XUID，而不是「现在有没有可用容器」。商店箱被拆了、被换掉了、或改了 `shopContainers` 配置，牌面也不该因此变成人人可改——否则一个便宜动作就绕开了保护。
- **用「像商店」而不是「现在严格合法」**：`looksLikeShopSign`（第 3 行有 B/S 且第 1 行非空，即 `ok()` 或错误不是「不像商店牌」），不是 `ok()`。把 `maxShopAmount` 调小、把 `adminShopName` 改名、或者牌子是外部工具写的脏数据，都不应该让保护整体失效。
- **两道闸，不是一道**：非店主不放（`mayEditShopSignText`），上了蜡的牌连店主与 OP 也不放。上蜡是一道对所有人的闸，因此与“你是谁”分开算。**蜡没有反向操作**：原版不能除蜡，所以这道闸的实际意思是「连店主也改不了，只能拆牌重立」。

判定只看店主 XUID 这一个真相源（ADR-0005），OP 放行。**记录不到店主 XUID 时四处口径不同，故意如此**：

| 场景 | 无主牌（记录缺失） | 理由 |
| --- | --- | --- |
| 改牌面文本 | 放行（fail-open） | 记录缺失的牌不该因此永久锁死；代价是被第一个改它的人接手（§5.4） |
| 表单管理入口 | 不给（fail-closed） | 一个查不到主人的店不该把「管理商店」按钮发给路过的每个人 |
| 染色 / 墨囊 / 上蜡 | 只给 OP | 这些改的是牌子本身，不是归属；没有必要为异常牌开一个装饰口 |
| 开箱 / 拆箱 / 拆牌 | 只给 OP（fail-closed） | 动的是库存与方块本身；查不到主人不该等于「谁都能搬」 |

物品类操作（染色 / 墨囊 / 上蜡）**与玩家站在哪一面无关**：上蜡改的是整块牌子，站在背面刷一下就能把店主的编辑权限冻住，所以判定只看「这一格是不是商店牌」。文字则不同，背面仍是布告板（§5.5）。

物品类操作的判据也因此不要求附着容器可用：「是不是商店牌」只看正面文本与记录的店主，容器只决定「现在能不能交易」。商店箱被拆掉时交易停摆，但牌子仍然属于它的主人。

**H3e 的可达性（真机第二报的更正）**：默认配置下的**合法商店**触发不到「拆依附方块」——商店牌永远贴着
它自己的那个容器，所以「拆依附方块」与「拆商店箱」是同一格，已被 `BreakChest` 那一档覆盖。这一条
**不是死代码**，它在下面这类状态里才是唯一生效的保护：

- 配置改了、容器不再合法（例如把 `minecraft:chest` 从 `shopContainers` 里删掉）→ `containerAt` 返回空
  → 这一格不再是「商店箱」，但牌面**仍然像**商店牌 → 走 H3e，非店主拆箱子还是被拦并提示
  「这块方块上挂着 X 的商店牌…」；
- 双箱配对状态异常、存档被外部工具改过等脏数据。

判据因此刻意分成两层：`isShopBlockAt`（商店牌 || 合法容器上绑着商店牌）判「这是不是商店方块」，
`shopSignsWithAttachedCell` + `looksLikeShopSign` 判「拆了这一格会不会让商店消失」。前者失配时后者兜底，
**改配置不该让已有的商店失去保护**（DESIGN §9.2 / §14 的既定退路）。

**客户端预演（真机首测踩到）**：染色 / 墨囊 / 荧光墨囊 / 上蜡这四样在客户端是**本地预演**的——玩家右键那一刻，客户端先把牌子画成“已经改过”的样子，服务端 `cancel()` 只能挡住改动真的写进去，挡不住这一页预演。不处理的话玩家看到的就是“改成功了”，退出重进才发现没改（而“没改”才是对的）。所以拦下之后必须把这一格牌子的真实数据再发一遍：`BlockActor::_getUpdatePacket`（原版改牌文本用的就是这个 `BlockActorDataPacket`），拿不到就先 `SignBlockActor::onChanged(region)` 把这一格标脏再问一次，仍拿不到就放弃。**绝不自己拼 NBT**——试过，拼出来的包会让牌子显示空白、编辑界面打不开，客户端还会掉线。只发给动手的那个玩家，不用广播。以后新增“拦下但客户端会预演”的交互（箱子上蜡、告示牌锁定等）都要补这一下。

> **同一类问题的第二处（2026-11-01 真机第二报）**：**拆**也会预演。对着商店牌或商店箱按下挖掘键时，
> 客户端先把「牌子碎掉、牌面文字没了」演出来；`PlayerDestroyBlockEvent` / 攻击事件被 `cancel()` 之后，
> 方块本身会回到服务端状态，**但方块实体（牌面文字）没回来**——现象是「拆不掉，但牌子空了，
> 退出重进才显示出来」。所以 `ContainerProtectionListener::reject(...)` 带一个 `resync` 开关，
> 拆牌/拆箱被拦下时也重发这一格的方块实体。
>
> **第三处（真机 D1 全量验收时发现）**：**改牌被拒绝**也是预演出来的——玩家敲完字点确定的那一刻，
> 客户端已经把这段文字写进本地那份牌子里了，`event.cancel()` 只挡住服务端写入，于是牌子看着「写进去了」，
> 退出重进才发现是空的。同理，**规范化成功的编辑**也有半个错位：客户端显示玩家敲的原文（大小写、空格、
> 缺省一侧都可能不一样），服务端存的是规范化后的文本。两者都靠同一招解决：
> - 拒绝 → `Before` 阶段 `cancel()` 之后立刻重发这一格；
> - 规范化 → `After` 阶段（文本已真写进去、归属也补完了）再重发这一格。
>
> **重发的实现约束（真机第四报换来的）**：**不许自己拼 NBT** 再发 `BlockActorDataPacket`。
> 早先的退路是 `save(tag, SaveContext{SendOverNetwork})` 自己组包，用来兜「`_getUpdatePacket`
> 返回空」的情形，真机后果很严重：牌子显示空白、**打不开编辑界面**、连点几次客户端直接掉线——
> 因为原版那个包依赖的不只是 `save` 出来的字段（刚放下、还没写过字的牌子尤其危险）。
> 现在的做法是**只让原版生成**：先问 `_getUpdatePacket`，为空就调告示牌自己的 `onChanged(region)`
> 把它标脏再问一次，还要不到就**放弃**——宁可让客户端多显示一次旧内容，也不发拼出来的包。
> 同一玩家对同一格的重发另有 250ms 节流（连点不该变成一串包，那是掉线的另一半原因）。
>
> 这几处共用同一份实现：`src/mod/BlockResync.h` 的 `resyncBlockActor(player, region, pos)`
> （首选 `BlockActor::_getUpdatePacket`，为空才 `onChanged` 后重试，仍为空就放弃；**只发给动手的那一个人**）。
> **规则**：任何 `event.cancel()` 拦下的、客户端做过的动作，都要问一句「客户端现在看到的东西还对吗」，
> 不对就补一次重同步——这已经不是个别用例，而是一条通用约束。
>
> **范围不是一种（2026-11-01 P1 起）**：上面这些是「拦下一个客户端预演过的动作」，只有动手的人看到的
> 东西需要回滚，所以单人重发就够。**服务端自己改写方块实体**是另一回事：管理表单改价（票 01）不是玩家
> 发起的编辑，客户端不会自己重画，而旁边的人会继续对着旧价格谈生意。所以这条路用
> `resyncBlockActorToDimension(region, pos)`：`forEachPlayer` 遍历，`getDimensionId()` 相同的都发一份，
> 不算距离、**也不吃 250ms 节流**（连改两次价时第二次不能被当成连点吃掉）。改价是低频操作，
> 几十个包不算事；漏发比多发严重得多。

上蜡只是**编辑开关**，不改变商店语义（D29）：上了蜡的牌交易照常，文本谁都改不了。

**上蜡不可逆（2026-11-01 修正）**：早期版本以为「斧头能除蜡、店主要先除蜡才能改」——**这是错的**。原版告示牌的蜡没有任何去除手段（斧头只对铜块有效；Mojira MC-261313「Wax cannot be removed from a sign using an axe」的结论是 Works As Intended；Wiki 也写「若要修改则需破坏后重新放置告示牌」）。因此：

- 斧头在牌面防护里就是**普通物品**：拿它点牌与空手点牌完全一样（正面弹表单、背面放行），不存在「除蜡」这个操作；
- 上过蜡的牌，牌面文字对**所有人**（含店主与 OP）都是只读；
- 要改只有两条路：拆掉这块牌重立（等于关店重新开），或者走管理表单（`P1`，见 `.scratch/p1-shop-management/`）——表单走的是服务端写牌，**不受蜡约束**；
- **因此上蜡只拦手写编辑，不拦管理表单**（票 01 落地）：蜡是给「手写容易写错格式」这类直接改文本的路径设的闸，
  不该顺带把经过完整校验的管理路径也堵死——原版没有除蜡手段，堵死了等于改一次价就得拆牌重立。
  牌面防护那几道（非店主、染色、上蜡）一个字不改。管理表单正文会写一句「这块牌上了蜡：手写编辑仍被锁，这里可以直接改」，
  免得店主以为牌坏了；
- 上过蜡的牌再做染色 / 改发光 / 再上蜡也一律不生效（原版拒绝），提示直接说「上了蜡」。

创建失败打掉牌子时（仅当 `removeInvalidShopSign = true`），按 Java 版 `breakNaturally` 语义**有掉落**（可配 `dropSignOnCreationFailure`，默认 true）。

### 9.3 判定"这个方块是否被商店占用"

- 方块本身是商店牌（正面文本像商店牌，`looksLikeShopSign`）→ 是。
- 方块是容器 → 查它六个相邻面（以及双箱另一半的六个相邻面）是否存在商店牌，且该牌**附着的正是它** → 是。反查只看附着格，不扫别的邻居（与 §7.1 同一条规则）。
- 保护判定用「像商店」而不是「现在严格合法」，也**不看 `shopContainers` 白名单**：改配置、把 `maxShopAmount` 调小、牌子是外部工具写的脏数据，都不该让一个已经存在的商店箱失去保护（理由同 §9.2 的牌面防护）。
- 判定为纯读取，无缓存，失败即放行（不误伤普通箱子）。

---

## 10. 交易流程

### 10.1 交互

1. `PlayerInteractBlockEvent` 命中有效商店牌 → `event.setCancelled(true)`。
2. 弹 `SimpleForm`（标题：`<店主> 的商店`；管理员商店用 `管理员商店`）：
   - 正文直接列出店主、商品、每份件数、买入价、卖出价、可成交份数、余额（经济后端未接入时显示占位，不编造数字）
   - 买家视角：`购买（1 份 N 个 / B 价）`、`出售（1 份 N 个 / S 价）`；缺哪一边价格就没有哪个按钮
   - 店主视角（店主与 OP）：`管理商店`（改价 / 停业），**没有**买卖按钮（店主不能跟自己交易）
   - **管理入口不看这个表格角色**：它只问 `canManageShop(shopActorOf(记录XUID, 玩家XUID, 是否OP, 管理员店))`。
     管理员店的 OP 在交易上是买家视角，但仍然是管理者，所以同一个人可能同时看到「管理商店」与买卖按钮。
   - **店暂时不能交易时（`unavailableReason` 非空）只藏买卖按钮，不藏管理入口**：否则管理员店一停业
     就一个按钮都没有，永远无法恢复营业（spec D5）。
   - **管理员商店例外：所有人都是买家视角**（包括 OP）。它的对手方是系统账户（7.4），
     “店主”不是交易的一侧；管理能力（改牌、开箱、拆箱）不看这个表格角色，仍然只给 OP（11）
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

> **坑（真机首测踩到）**：`CustomForm` 的下拉框回调**回传的是选中项的文案，不是下标**——
> `ll::form::CustomForm` 里 `Dropdown::parseResult` 返回的是 `mOptions[index]`（`std::string`），
> 只有当传入的不是整数下标时才是空串。按下标（`uint64`）读会永远拿不到值，全部档位都报「份数选择不对」。
> 所以 `src/shop/UnitsForm` 提供 `choiceIndexForOption`（`unitsFormOptions` 的逆运算）把文案换算回下标，
> `src/mod/ShopForm` 两种回传形式都接住。以后新增任何下拉框都要走这条换算。

> **P0 现状（06 落地后）**：店主的「改牌面」入口是**潜行 + 右键**。两种手势的差别只有这一处：普通右键弹表单，潜行右键不进表单、原版编辑界面照弹（上蜡时拦下并说清只能拆牌重立）。非店主潜行右键仍然弹表单——潜行不该成为绕过保护的通道。`管理商店` 按钮在 P1 之前是假按钮（只发一句「待实现」）。
>
> **P1 现状（票 01 落地后）**：普通右键弹出的表单里，「管理商店」真的能进了。
> - 管理表单正文复用信息表单那一份 `ShopInfoData`（店主 / 商品 / 每份 / 买卖价 / 可成交份数 / 余额），
>   不重算库存——店主改价时才看得到「现在还卖得动吗」；上蜡的牌多一句「手写编辑仍被锁，这里可以直接改」。
> - 本票只有一个入口「改买入价」：`CustomForm` 一个输入框，初值 = 当前买入价，只收**非负整数或留空**
>   （留空 = 这一侧不做生意；牌面写法 `B 10` / `free` / `1.5K` 一律不收，方向由输入框表达）。
> - 提交时**从牌面重新解析**（表单开着时牌可能已被别人改过），再用 `withBuyPrice` 合成新的 `ShopSpec`，
>   最后交给 `formatShopSign` 规范化——与手写编辑同一套解析与规范化，没有第二条格式化路径。
>   两侧都清空、卖出价高于买入价、输入不合法都在这里拒，**失败时牌面一字不改**。
> - 写入用 `SignBlockActor::setMessageForServerScripingOnly(Front, text, ownerID)`：普通店传店主 XUID，
>   管理员店传空串（它的归属靠第 1 行 + 编辑者是 OP）。第 1 行与第 4 行沿用牌面原文，商品由容器决定。
> - 写完把这一格发给**整个维度**（`resyncBlockActorToDimension`，见 9.2）：这不是玩家发起的编辑，
>   客户端不会自己重画，旁边的人会对着旧价格谈生意。
> - 潜行 + 右键那条路**一个字没改**，仍然是并列的第二条写入路径。
>
> **P1 现状（票 02 落地后）**：管理表单多两个入口，三个改动走同一条链。
> - 「改卖出价」：`CustomForm` 一个输入框，初值 = 当前卖出价，留空 = 这一侧不收购（第 3 行只剩 `B 10`）。
> - 「改每份件数」：初值 = 当前件数，只收 `1..maxShopAmount`（与手写编辑同一份 `parseAmountLine`）；
>   空、非整数、`0`、`maxShopAmount + 1`
>   一律给「一份的件数要写 1 到 N」的原因，**不夹到边界**（件数没有「留空 = 不做这一侧生意」这一档）。
> - 三条校验只有一处：输入非法、两侧都清空（第 3 行变空 → 牌与箱子整体失去保护，spec D6）、
>   卖出价高于买入价。前一条的提示会说明「想关店请拆牌，或者停业」，而不是让店主把价格清空。
> - 三次改动共用票 01 的合成与写牌：`withBuyPrice` / `withSellPrice` / `withAmount` 只换落哪个字段，
>   `formatShopSign` 与 `setMessageForServerScripingOnly` + `resyncBlockActorToDimension` 各只有一处。
>   第 1 行（店主名）与第 4 行（商品名）照旧一个字不动。
>
> **P1 现状（票 03 落地后）**：管理表单多一个「停业 / 恢复营业」按钮（按当前状态只给一个）。
> - 停业状态写在第 3 行末尾的 `:x` 上（5.2），**价格、归属、库存、保护一个字都不改**；恢复营业就是把那一段去掉，
>   所以「原价一分不差」是结构上成立的，不靠内存记着。牌子即数据源，重启后仍然停业。
> - 这一项**不需要输入框**，但它走的是与改价同一条提交链（重新取牌 → 重新解析牌面 → 规范化 → 写牌 → 发包），
>   没有第二条写牌路径；四个改动最后都收在 `applyManageEdit` 一处校验。
> - 停业时点牌：正文多一行「这家店暂停营业」，**一个买卖按钮都不给**，但**管理入口照给**
>   （管理员店的 OP 在交易上是访客，入口一消失就永久停业，只能拆牌）。
> - 交易层两道闸用同一条判据（`TradeRejection::ShopClosed` → 「这家店暂停营业」）：`planTrade` 最前面一条，
>   提交阶段再判一次（防停业之前打开的旧表单）。
>
> **P0 现状（11 落地后）**：管理员店与普通店走同一张表单、同一套算法，差别只在三处：
> 资金对手方是系统账户（买卖两边的钱靠销毁 / 凭空记账，7.4）、不抽商店税、不吃离线策略。
> 库存仍然来自容器，所以“卖光”就是箱子空：正文写「商店箱里没有货」，一个按钮都不给。

> 牌上第 2 行是**一份**的件数，表单里选的是**份数**。这是 Java 版"右键一次成交一整份"的等价物；Java 的 shift 行为（按单价重算任意数量）不在 P0，因为它会引入小数价格。

### 10.2 执行顺序（预校验 + 快照 + 双向补偿）

物品与余额不在同一个事务里，**任何"先做 A 再做 B"都有中间态**。实现按以下顺序，并把补偿失败当成严重错误处理：

```
1. 解析商店（牌子 + 容器 + 商品 + 价格）            // 失败 → 提示无效商店
2. 预校验：余额、库存、背包空间、冷却、权限          // 失败 → 直接返回，无副作用
3. 计算可成交数量 n（部分成交开关）                 // n <= 0 → 提示缺货/缺钱/背包满
4. 快照：玩家背包与容器受影响槽位的 ItemStack 副本    // 只为补偿用
5. 金额操作：玩家 ↔ 店主（优先用后端的原子 transfer）  // 管理员店：玩家 ↔ 系统账户（销毁 / 凭空记账）
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
- 商店税（§8.9）是第 5 步的后半段，但**排在物品那一步之后**：税一旦销毁就补不回来，
  先收税会让「物品失败」时的退款短一笔税。收税失败则把物品与主交易一起退回，款额按实际移动过的金额算。
- 若后端不支持原子 `transfer`，用 `withdraw` + `deposit` 手动实现，并保证 `deposit` 失败时把 `withdraw` 的钱退回。
- 补偿逻辑必须在同一次事件处理内完成，不依赖重启恢复。

### 10.3 部分成交（对齐 `ALLOW_PARTIAL_TRANSACTIONS = true`）

- 买：`n = min(请求量, 库存, 背包容量, 余额可承受量)`。
- 卖：`n = min(请求量, 玩家持有量, 容器剩余空间, 店主余额可承受量)`。
  （最后一项是卖出特有的：卖出是店主付钱，钱不够就只能少收一点或者整笔拒绝。
  管理员店的资金侧不设上限，这一项不存在（`shopSideFunds` 返回 `kUnlimitedFunds`）。）
- `n` 与请求量不同时，总价按 `单价 × n` 重算，并在确认表单里提前显示。
- 关闭部分成交时，`n < 请求量` 直接失败，并且**要说清是哪一条不够**：
  「货不够 / 余额不够 / 装不下」是三件事，不能一律报成「输入非法」；
  买入时是「商店箱里的货 / 你的余额 / 背包」，卖出时是「你手里的货 / 店主的余额 / 商店箱」。
  （`planTrade` 一份实现里就分好了「哪条上限挡住了」，只有文案按方向换说法。）

> **真机发现（2026-11-01）：正常路径下请求不到超额份数。** 份数表单的上限不是「单次上限」一个数，
> 而是**四者取最小**（`maxTradeUnits`：`maxShopAmount`、货量、能收下的份数、付得起的份数，
> 卖出方向的付得起含税）。箱里只剩 5 份时输入 8，得到的是份数表单自己的输入校验
> 「自定义份数要在 1 到 5 之间」——因为上限已经把货量算进去了，走不到交易阶段。
>
> 所以「部分成交 + 先确认」这条路径**只在表单打开期间状态变了**才成立：你开着份数表单，
> 另一个玩家把货买走 / 补了货 / 塞满了箱子，或者你的余额、背包在那段时间里变了 → 服务端重新规划
> 后发现实际能成交的份数与所选不同 → `NeedsConfirm` 弹确认并说明哪一项变了（份数 / 件数 / 总额），
> 确认后按**新的实际值**成交；关掉 `allowPartialTransactions` 时同样重新规划，但**不夹份数**，
> 整笔拒绝并说清哪一条不够。
>
> 这条设计的意义：正常玩家永远不会在不知情的情况下被部分成交（上限先挡住），并发改动必须经过
> 一次显式确认——两种风险各自被最合适的那一层挡住。

### 10.4 反刷保护

- 同一玩家 250ms 内重复提交 → 忽略（对齐 `SHOP_INTERACTION_INTERVAL`）。
- 店主不能在自己的商店交易（可配）。管理员店不适用：它的对手方是系统账户（7.4），OP 也能买卖。
- 创造模式玩家默认禁止交易（可配）。
- 表单提交是权威操作，不信任客户端传来的价格。

---

## 11. 权限模型

| 角色 | 判定 | 能力 |
| --- | --- | --- |
| 游客 | 其他玩家 | 买卖、查看商店信息 |
| 店主 | **正面记录的编辑者 XUID == 玩家 XUID**（ADR-0005） | 编辑牌面、染色、上蜡、开箱、拆店、管理商店 |
| OP | `Player::isOperator()` | 全部，包括创建/管理管理员商店；在管理员店里也是买家（对手方是系统账户，7.4） |
| 管理员商店 | 第 1 行写 `adminShopName` **且创建者是 OP** | P0：库存来自容器，资金侧是系统账户；管理（改牌、开箱、拆箱）只给 OP |

- **不解析玩家名**。店主身份直接来自告示牌正面的 `SignBlockActor::Text::mLastEditXuid`（引擎自己持久化的字段），与第 1 行的文本无关。第 1 行只是显示，规范化时被覆盖成该 XUID 的当前名字。
- 因此玩家改名不影响商店归属，也不需要 `NameCache` 或任何外部映射表。
- 代价：不能通过手写别人的名字来开店。**P0 不能转让**：改牌面受店主权限约束（§9.2），所以“让对方来改这块牌子”这条路在 06 之后走不通；要换人只能拆牌重开（拆牌是店主权限）。ADR-0005 落地时的“转让 = 对方改牌”在那时成立，现在被编辑防护挡死了，已在 ADR-0005 末尾补了注。
- 非 OP 在牌上写 `adminShopName` 时，这一行按普通玩家名处理 —— 由于第 1 行本来就只是显示，结果是"普通商店，店主是写牌的人"，不会被误判成管理员商店。
- 已有商店牌被非店主编辑时：交互阶段拦下（不弹 UI）+ 提交阶段兜底取消，见 9.2。
- 店主的「改牌面」入口在 P0 是**潜行 + 右键**（10.1 的 P0 现状）：普通右键给表单，潜行右键给原版编辑界面。非店主两种手势都弹表单。
- 「谁是店主」只有一处真相源（ADR-0005）：交互阶段（表单角色）、提交阶段（能不能改牌）读的是同一个 `mLastEditXuid`，不另造一套判定。
- 管理员店不吃「店主」这一套：**只有现任 OP 算管理身份**（牌上记的 XUID 被撤权后不再有管理入口），
  但**买卖对所有人开放**（包括 OP）——表单角色由 `shopFormRoleOf(actor, adminShop)` 判，
  管理员店一律访客。管理能力仍然由 `shopActorOf` / `canManageShop` 判，不靠表单角色。

---

## 12. 实施计划

### P0：可玩闭环

0. 工程骨架：`levilamina 26.51.8`、iListenAttentively 依赖、`ss::shop` 命名空间、配置加载（**已完成**）。
1. `ShopSign`：四行解析、校验、规范化。**没有物品名解析**（商品来自容器，见 5.3）。
2. `ShopOwner`：读正面 `mLastEditXuid`，解析成 XUID + 当前显示名。
3. `ContainerFinder`：墙牌背面优先 + 六面顺序 + 双箱合并 + "一个容器只能被一个牌绑定"校验。
4. `ShopService`：创建/编辑/销毁、多商店规则、保护判定、上蜡状态查询。
5. H1：监听 `PlayerEditSignBeforeEvent` → 创建/编辑（改 `text()` 完成规范化），并作提交阶段的兜底闸（非店主 / 上蜡的牌丢弃包，见 9.2）。
6. 交互与编辑拦截：`PlayerInteractBlockEvent` → 表单（`SimpleForm` + `CustomForm`），并拦下非店主的编辑/染色/墨囊/上蜡；店主与 OP 潜行右键进原版编辑界面（10.1 的 P0 现状）。
7. 交易：预校验 + 快照 + 转账 + 搬物品 + 补偿 + 刷新；份数制计价（10.1）。
8. 保护：开箱、拆牌、拆箱、H2 漏斗、H3 方块级爆炸/活塞/凋灵/火焰。
9. 经济后端：`LegacyMoneyEconomy` + `ScoreboardEconomy` + `NullEconomy` + 探测与回退（第 8 节，**不是内存桩**）。
10. 管理员商店：OP 建、库存来自容器、资金侧是系统账户（买入销毁 / 卖出凭空记账）、
    OP 也能买卖（7.4 / 11）。
11. 手工验收（第 13 节）。

### P1

- 空店清理、创建费、退款；`tax.taxDestination` 指向税收账户（§8.9）
- 管理员商店的无容器模式、系统账户作为**真实**账户（余额可充可查）、给系统账户的管理入口
- shift 快捷成交（按单价重算任意数量，需引入小数价格）
- 交易流水 + 平均价格
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
19. 店主执行 14~18 → 全部允许（改文本用**潜行 + 右键**，见 10.1 的 P0 现状）。
20. 上蜡后的商店 → **谁都改不了**（原版去不掉蜡），商店功能照常可交易；要改只能拆牌重立。
20a. 非店主**潜行 + 右键**点商店牌 → 仍然只弹表单，不弹原版编辑界面（潜行不是后门）。
20b. 上蜡后店主潜行 + 右键 → 被拦下并提示「上了蜡，要改只能拆掉这块牌重立」；拿斧头点牌只是普通点击（弹表单），不会除蜡；店主也改不了蜡（原版无此操作）。
20c. 非店主站在商店牌**背面**拿蜜蜡点牌 → 牌子不上蜡（上蜡是整块牌子的事，不看玩家站哪一面）。
20d. OP 改一次别人商店的牌面（走正规路径）→ 牌还是原店主的，第 1 行显示的名字与归属都不变。
20e. A 先在一块牌上写普通公告，B 再把它改成合法商店 → 店主是 **B**（归属写给把它变成商店的人）。
     店主把牌改回普通文本等于**关店但不拆牌**：容器随文本一起失效，任何人在那格重写成商店就是新店主。
20f. 拆掉/换掉商店箱之后，非店主仍然改不了牌面、上不了蜡（保护只看「像不像商店牌」，不看容器）。
20g. 非店主拿斧头点一块**没上蜡**的商店牌 → 任何东西都不变，只会弹商店表单（那一下本来就没蜡可除）。

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
29a. 管理员店（OP 建，箱里有货）：普通玩家买 → 背包加货、箱子减货，玩家余额**变少**且没有任何人收到那笔钱；
     同一玩家卖 → 背包减货、箱子加货，玩家余额**变多**。两种方向都不看创建者是否在线。
29b. 管理员店买光之后（箱子空/不足一份）→ 提示缺货或「商店箱里没有货」，三者不变；补货后恢复。
29c. 管理员店的创建者（OP）自己点牌 → 拿到买卖按钮（不是「管理商店」）；改成管理员店前他还能改牌、开箱。
29d. 管理员店 + 记分板后端：买卖同样成交（系统账户靠加/减分实现）；没有可用系统账户能力时
     点牌正文写「当前经济后端没有系统账户」且一个买卖按钮都不给。

**停业 / 恢复营业（票 03）**

29e. 店主在管理表单点「停业」→ 牌面第 3 行末尾出现 `:x`（`B 10 : S 5 :x`），价格一位不改，库存与归属不动。
29f. 停业期间买家点牌 → 正文写「这家店暂停营业」、一个买卖按钮都不给；拿停业之前打开的份数表单提交 →
     同样被拒（「这家店暂停营业」），钱与货一分不动。
29g. 停业期间非店主改文本 / 染色 / 上蜡 / 开箱 / 拆箱 / 拆牌 → 全部照旧被拦（牌子仍然「像商店牌」）。
29h. 停业 → 服务器重启 → 仍然停业（状态只在牌面上）；管理员店的 OP（交易上是访客）仍然看得到「管理商店」，
     点进去是「恢复营业」→ 恢复后原价一分不差、库存不动。
29i. 手写编辑价格行写 `B 10 : S 5 :x` → 生效并规范化成同样写法（它就是合法价格行，不需要额外权限）。

**保护**

30. 非店主开商店箱 → 打不开。
30a. 店主开自己的商店箱 → 能开；把 `allowOwnerOpenChest` 改成 `false` 后店主也打不开（提示指向这个开关），OP 仍然能开。
31. 非店主拆商店箱 → 拆不掉。
32. 非店主拆商店牌 → 拆不掉。
33. 店主拆牌 → 成功，箱子恢复普通箱子。
34. 拆掉双箱一半 → 商店失效，牌子物理掉落（两半各试一次：挨着牌子的那一半、以及不挨着的那一半）。
34a. 普通箱子与普通告示牌不受影响：开箱、攻击、拆除都照常。
35. TNT 在商店箱旁爆炸 → **商店箱幸存，旁边的方块照常被炸掉**。
36. 活塞推商店箱 → 推不动；推旁边的方块 → 正常推动。
37. 凋灵破坏 / 火焰 → 商店箱不受影响，**而范围内的其他方块照常被毁**（整场哑火也算不合格，见 §6.2 H3c）。
38. 漏斗方块在商店箱上方抽、下方塞 → 都无效。
39. 漏斗矿车停在商店箱下方 → 吸不走货（与漏斗方块共用同一拦截）。
40. 铜傀儡走到商店箱旁 → 取不走也放不进。
41. 投掷器朝商店箱喷物品 → 塞不进去，物品落到地上。
42. 非店主拆掉商店牌背后那块墙 → 拆不掉（牌子不会掉落）。
42a. 店主拆掉那面墙 → 可以拆，牌子物理掉落，商店消失（与拆牌等价）。

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
| 经济后端 | 已落地（10） | LegacyMoney 优先、记分板兜底、空实现垫底；离线店主按 8.5 处理。真实行为待 12 验收 |
| 管理员店的系统账户（生 / 灭钱） | **已真机验证（2026-11-01）** | 判据是 `supportsSystemAccount()`。真机结果：买入 `mtrans` 是 `玩家 → System 金额`（钱销毁，店主余额不动），卖出是 `→ 玩家 金额（add N）`（钱凭空生，无付款方），两个方向都没有出现「系统账户付款失败」 |
| `LLMoney_Add` 对从未上账的 XUID | **源码证伪风险（2026-11-01）** | `LLMoney_Add(xuid, n)` 走 `LLMoney_Trans("", xuid, n)`，空付款方分支先 `LLMoney_Get(to)`——而 `Get` 查不到行时会 `insert` 一行 `def_money`——再加钱。所以从未上账的 XUID **能加**，不需要给系统账户预建行。依据：LegacyMoney `src/API.cpp` |
| 后端静默降级导致账本换家 | **已移除（2026-11-01）** | 老默认值 `auto` = 「LegacyMoney 探不到就用记分板」，会让账本一夜之间换一套（余额看起来全变 0）且只在日志留一条 info。现在后端必须显式选、一个只探一个、探不到就报错不启用交易（§8.4） |
| LeviLamina 目标版本 | 已定 `26.51.8`（兼容 `>=26.51.7 <26.60.0`） | iListenAttentively 0.17.0、行为以实际头文件为准 |
| 商店税与 `pay_tax` 双重扣税 | 已知，**无法程序化检测** | `shopTaxRate > 0` 时无条件 WARN 要求设 `pay_tax = 0`；不关就接受「店主被抽两次、补偿漂移」，P0 不自动禁用商店税（见 8.9） |
| `LLMoney_Get` 的失败通道 | 已核实（LegacyMoney `src/API.cpp`） | 空 XUID 或数据库异常返回 `-1`，本模组据此返回「读不到」；从未上账的 XUID 会被它插入一行 `def_money` 再返回，所以「新账户」读到的是默认金额而不是「读不到」 |
| LegacyMoney 导出名 / 版本不匹配 | 已核实 | x64 的 C 导出是未修饰名（`LLMoney_Get` 这一组，已对照 `LegacyMoney.dll` 导出表）。拿不到就判 `ExportsMissing` 并降级，**不猜 mangled 名**（另一套是 C++ 命名空间版） |
| 没装 LegacyMoney 的服务器加载失败 | 已解决 | 绝不 `add_packages("legacymoney")`、绝不 include 它的 `LLMoney.h`（`dllimport` 会在 `Shop.dll` 留导入项）；只用 `GetModuleHandleW` + `GetProcAddress`，构建后确认 `Shop.dll` 导入表里没有 LegacyMoney |
| LegacyMoney 运行期被卸载 | 已知，**无法彻底防** | 每次动钱前重新 `GetModuleHandleW`：不在了就按「读不到 / 拒绝」处理并报一次 error，不去调悬空的函数指针。检查与调用之间仍有理论窗口 |
| H2/H3 自定义 hook 的符号漂移 | 已知 | 每个 LeviLamina 大版本回归一次；符号在 26.51.8 头文件中已确认存在 |
| 凋灵逐格剔除的落点 | 待实测 | 逐格剔除只能落在 `Level::$destroyBlock`（`_destroyBlocks` 只给整个 `AABB`）。若凋灵的销毁不走这条路径，商店箱会被毁；退路是回到提前返回（代价：凋灵范围内旁的方块不毁），见 §6.2 H3c |
| 依附方块的物理掉落 | 待实测 | 拆掉商店牌依附的方块后，牌子是否真的由原版物理打落（以及掉落物是否尊重 `doTileDrops`）；若不打落，则「店主拆时与拆牌等价」不成立，需改成拆方块时显式打牌 |
| 爆炸/活塞不保护「依附方块」 | 已知，P0 接受 | H3e 只盖玩家拆除路径；TNT 炸掉牌子背后那块石头、或活塞推走它，牌子仍会掉落、商店仍会消失。P1 若收紧，把 `cellsToSpare` 的判定从「这一格是商店方块」扩成「这一格是商店方块或是商店牌的附着格」 |
| 编辑拦截的用户体验 | 待实测 | 交互阶段拦下可以不弹 UI，但需确认 `PlayerInteractBlockEvent` 的取消确实阻止 `OpenSignPacket` |
| 包路径是否写入 `mLastEditXuid` | 待实测 | `setMessageForServerScripingOnly` 需要显式传 `ownerID`；若包路径不写，03/06 需要在写入后补一次显式 ownerID |
| 侧挂的悬挂牌无法从状态判定墙面 | 已知，P0 不支持 | 基岩把两种悬挂形态合并成一个方块 ID，侧挂时 `facing_direction` 与墙面垂直；猜错会误绑旁边的箱子 |
| 上蜡状态读取 | 已确认字段 | `SignBlockActor::mIsWaxed`（bool），无 xuid 记录 |
| 并发/线程安全 | 待验证 | 表单回调切主线程执行方块操作 |
| 拆箱时手动打掉商店牌 | 待实测 | 在 `PlayerDestroyBlockEvent` 里调 `Level::destroyBlock` 毁牌；确认不会与原版物理/双箱处理重复掉落，且 `dropResources = true` 仍尊重 `doTileDrops` / 创造模式（验收第 34 条顺手看） |
| `PlayerDestroyBlockEvent` 的触发时机 | 待实测 | 事件里能否读到 `ChestBlockActor::mLargeChestPaired`；若方块先被移除再发事件，反查会拿到空列表 → 商店箱静默放行、手动打牌也不执行（12 有验收项）。手动打牌是不可回滚的副作用：我们自己打完牌后若另一个监听取消了这次拆除，会留下「牌已掉、箱子还在」 |
| 内容防偷五条 hook 的符号 | 待实测 | `Hopper::_tryPullInItemsFromAboveContainer` / `_pushOutItems`、`DropperBlockActor::pushOutItems` / `getContainerAt`、`TransportItemsGoal::_executeEvent` 在 26.51.8 头文件里都存在，但 `hook()` 的返回值不是契约（见 08 的同一条）。注册非 0 会留一条 warn，真正的判据是 12 的逐条验收 |
| 投掷器的拦截依赖两层 | 待实测（本票最脆的一环） | 内层 `getContainerAt` 若不是 `pushOutItems` 的必经之路，投掷器会照常把货塞进商店箱。拦不住时 `pushOutItems` 会打一条 warn（不是静默）；退路是在入口就返回，代价是物品留在投掷器里、不落地 |
| 铜傀儡的拦截点是不是搬运闸门 | 待实测 | `TransportItemsGoal::_executeEvent` 是 `const` 的通知式接口，「即将成功的取/放」是否真的由它触发搬运，从头文件看不出来。若实测挡不住，退路是 `$canContinueToUse` 在 `mState == Interacting` 且目标是商店箱时返回 false（引擎自己 `stop()`，代价是这一趟运输白跑） |
| 铜傀儡丢掉事件后会不会卡在交互中 | 待实测 | 同一个 `_executeEvent` 也可能是状态推进点（丢掉 `FinishedInteraction` 会卡住，正说明这一点）。丢掉 `Start*` 后 goal 有可能停在 `Interacting` 等一个永不开始的交互，表现为傀儡在箱子旁反复开箱或停住——那样它虽然搬不走货，却是票面第 7 条要避免的形态。12 有专门验收项 |
| 铜傀儡只认 `mTargetContainerPosition` | 已知 | 拦截判据只看 goal 记下的目标容器坐标。这个字段为空时我们放行（正常状态下此时也不会搬运）；若真机观察到「货被搬走但该字段为空」，说明它用了别的字段或别的路径，需要在 12 里记下来并补一处判据 |
| 投掷器的坐标口径 | 待实测 | 内层 `getContainerAt` 收到的 `Vec3` 若不是目标格的方块原点（中心点或带偏移），我们会既挡不住、也可能没报——所以 warn 的触发条件改成「这次没真正挡到」（`reached == false`），并且分开报告「从没被问过」与「问了但坐标对不上」两种失效 |
| 漏斗 `attachedFace` 的方向口径 | 待实测 | 沿用 ChestLock 的读法：`position` 是装置自己、`attachedFace` 是搬运方向。若真机发现漏斗往商店箱里塞货仍然成功，说明这一口径反了，把 `cellInDirection` 的入参取反即可（一处改动） |
| 漏斗矿车与方块漏斗的同一性 | 待实测 | 票面要求一条 hook 同时覆盖两种形态（两者共用 `Hopper` actor 的逻辑）。这条假设来自头文件里 `Hopper::mIsEntity` 与两种形态共用同一批抽取/推出函数，需要真机各验一次 |
| 「贴牌占箱子」 | 已知，P0 接受 | 保护从牌子推导（ADR-0002），P0 不记录箱子原主；任何人给尚未绑定的箱子贴一块像商店的牌，就能把它变成自己的商店箱，原主只能找 OP。与 Java 版 ChestShop 同为生态行为，P1 可考虑开店收费 / 领地插件 |

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
    "backend": "legacy_money",
    "scoreboardObjective": "money",
    "createObjectiveIfMissing": true,
    "requireOwnerOnline": true,
    "currencySymbol": "$"
  },
  "tax": {
    "shopTaxRate": 0.0,
    "taxDestination": "void"
  }
}
```

> 草案里的键随对应 ticket 落地。没有被实现读取的键（`protectShopChest`、`preventHopperExtraction`、
> `allowMultipleShopsAtOneBlock`、`allowOneContainerPerShop`、`itemNameLocalized`、`removeInvalidShopSign`、
> `dropSignOnCreationFailure`、`tax.taxDestination` 等）目前**不生效**——写进 `config.json` 也不会改变行为。
> `preventHopperExtraction` 尤其要注意：内容防偷（09）**不读这个键**，因为 §9.2 规定漏斗 / 铜傀儡 / 投掷器对商店箱一律阻止（含店主与 OP），没有「关掉就不再防」的开关。
>
> 已生效的键：`adminShopName`、`shopContainers`、`maxShopAmount`、`allowPartialTransactions`、
> `blockSellPriceHigherThanBuy`、`allowOwnerOpenChest`、`shopInteractionCooldownMs`、`ignoreCreativeMode`、
> `economy.backend`（`legacy_money` / `scoreboard` / `null`；`builtin` 是 P1，写它会拒绝加载；
> **`auto` 已删除**，老配置写它会被拒绝并提示改成哪个）、
> `economy.scoreboardObjective`、`economy.createObjectiveIfMissing`、`economy.requireOwnerOnline`、
> `economy.currencySymbol`、`tax.shopTaxRate`。
>
> 两条配套约束：`economy.createObjectiveIfMissing` 会真的在服务器的记分板上建一个目标（默认 `true`，
> 选了 `scoreboard` 就是想要记分板记账，为了一个 `dummy` 目标再让服主手打命令没有收益）；
> `tax.shopTaxRate > 0` 要求服主同时把 LegacyMoney 的 `pay_tax` 设为 0（本模组读不到那个值，只能启动时 WARN）。
>
> **文件生成与迁移**：`config.json` 不存在时 `ll::config::loadConfig` 会先写一份带 `version` 的全默认文件再读回来。
> 手写一份只填几个键的文件是可以的，但**必须带 `"version": 1`**：缺这个键时 `loadConfig`
> 会拿默认值合并进内存并返回 `false`（不是读失败），本模组会打一条 warn、把补齐后的完整文件回写一次，
> 然后照常启动。改配置后一律重启服务器，配置只在模组 `load` 阶段读一次。

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
2. **潜行直接放行**：ChestLock 在 `PlayerInteractBlockEvent` 回调开头 `if (Sneaking) return;`，
   意味着潜行右键可以绕过锁。商店交互不能用这个分支：潜行对非店主仍然弹表单（不放大权限面），
   只对店主/OP 有意义——它是 P0 里进原版编辑界面的手势（见 10.1 的 P0 现状）。
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
| D20 | `shopTaxRate > 0` 时无条件 WARN，要求手动设 `pay_tax = 0` | LegacyMoney 无公开 getter，无法程序化检测；读不到就不猜，**不**自动禁用商店税，双重抽税的风险由服主承担（见 8.9 / 14） |
| D21 | 事件层依赖 iListenAttentively（不照抄源码） | 官方维护、跟随 LL 升级；未安装则拒绝启用（见 6.4） |
| D22 | `levilamina` 目标 `26.51.7+` | 同时满足 iListenAttentively / LSE / EconomyBridge 的下限 |
| D23 | 数量语义为「份」：牌上第 2 行 = 一份的件数，牌上价格 = 一份的总价 | 对齐 Java 版默认语义；表单选份数，总价恒为整数。例外：价格行允许小数，向下取整后再参与计算（见 5.2），因此仍然不会出现非整数总价 |
| D24 | 只有 OP 能创建管理员商店 | 基岩版没有权限节点，OP 是唯一可用判据；非 OP 写 `Admin Shop` 按普通商店处理（见 11） |
| D25 | 破坏保护做到方块级，**故意偏离 Java 版** | Java 命中受保护方块就取消整场爆炸/推动；基岩服里整场哑火会被当 bug（见 ADR-0004、6.2） |
| D26 | 店主 = 正面记录的编辑者 XUID，第 1 行只做显示 | 玩家名可改，XUID 稳定；避免外部 name→XUID 映射表（见 ADR-0005） |
| D27 | 商品权威是容器第一个非空槽位，牌面第 4 行只做显示 | 基岩没有稳定的物品 ID 文本；避免本地化名反查与同名不同物（见 5.3） |
| D28 | 一块告示牌一个商店，只用正面；背面是布告板 | 规则一句话说完；避免"第 5 行在背面"的反直觉拼接（见 ADR-0006） |
| D29 | 编辑/染色/墨囊/上蜡一律要求店主或 OP（无主牌例外：文本 fail-open，见 9.2）；上蜡不改变商店语义，且**不可逆** | 引擎的上蜡是编辑冻结，不是权限；原版不能除蜡（Mojira MC-261313 判 WAI），所以上蜡本身也必须归店主权限，否则路人刷一下就把店主的牌永久锁死 |
| D30 | P0 管理员店的资金端 = **系统账户生 / 灭钱**（空账户键）：买入销毁、卖出凭空记账 | GLOSSARY「资金侧不设上限」的字面实现，也是「无容器模式放到 P1」之外唯一能在 P0 让两个方向都成立的方案。判据是后端能力 `supportsSystemAccount()`（不是 `supportsMoneySink()`，后者只关系商店税）；管理员店不收商店税 |
| D31 | 管理员店对所有人开放买卖，**包括 OP**（表单角色一律访客）；「不能跟自己交易」不适用 | 对手方是系统账户，店主不是交易的一侧；管理能力仍由 `shopActorOf` / `canManageShop` 判，与表单按钮解耦（见 10.1 / 11） |

## 19. 诊断与状态（P0.1）

手工验收最难的部分不是"做操作"，而是"判断这次操作到底发生了什么"：`latest.log` 不实时刷盘、
hook 注册只在失败时 warn 一次、经济后端的选择只在启动那一刻打一行。所以补一个只观测、不改行为的观测包。

### 19.1 `/shop status`

OP 权限（`GameDirectors`）随时可查：模组版本、iListenAttentively 版本、配置关键 8 项、
经济后端的「配置值 → 选用值」（与启动日志同一句 `backendSelectionText`）、10 条 hook 的
**已尝试注册 + attach 返回码**、调试开关状态。

两处措辞必须守住：

- hook 只写「已尝试注册，attach 返回 N」。`HookSlot::attached` 只代表"挂过了"，
  `attachHooks` 的注释也写明返回值不判成败——**行为是否生效只能手测**，这条命令不许把它说成"挂载成功"。
- 世界未就绪时后端写「尚未解析（世界未就绪）」，不编值、不报错。

配置非法导致模组没加载时这条命令不存在（模组没加载就没有命令），别误判成"命令坏了"。

### 19.2 `/shop debug on|off`

不持久化、不做成配置项（配置要重启才生效，而调试的价值是"现在就想看"），模组关闭时归零。

只在**结果点**打一行 `[shop/dbg] <kind> k=v ...`，五类事件：

| kind | 时机 |
| --- | --- |
| `sign_decision` | 牌面点击判定完，**每个最终分支只记一次**（`TakeOver` / `PassThrough` / `Block` / `NotAShopSign`；冷却期连点带 `cooldown=true`） |
| `protection_block` | 保护拦下之后：`open_chest` / `break_shop` / `break_support` / `sign_color` / `sign_style` / `sign_wax`，带完整坐标，原因复用 `decision.message` |
| `sign_edit` | 告示牌提交阶段的判定结果：`outcome=NotAShopSign|Rejected|Normalized` + `pos` + `reason`。加它是因为编辑路径原本**一行日志都没有**，真机上「写进去了没」「为什么被拒」只能靠猜（2026-11-01 吃过这个亏） |
| `trade_result` | `finishTrade` 拿到结果时（`ok` / `fail` / `confirm` / `frozen`） |
| `content_block` | 漏斗拉/推、投掷器、铜傀儡被拦下的那一次 |
| `economy_resolution` | 后端解析后（含「认不出配置 / builtin / null」这些早退分支） |

刻意**不**逐步记录 `moveMoney` / `undoAndRefund` 的中间步骤：补偿路径的中间态会误导人
（"钱扣了"不代表最终扣了）。每 tick 会走到的位置（漏斗、投掷器、铜傀儡）用节流版，
连续相同的行 1 秒内只出一条，否则红石时钟一秒能刷 20 行。

### 19.3 解析时机（真机修正）

`ServerStartedEvent` **不可靠**：真机（LL 26.51.9）世界已就绪、玩家已进服，`/shop status` 仍显示
「尚未解析」，即这个事件没有在世界的就绪时刻送达；启动日志里也从来没有出现过那句选型行。
原来的设计依赖「世界一就绪就探一次」，实际会退化成「等第一次交易」——没交易过的服一整天都显示没后端，
排查时极易误判成「后端不可用」。

因此解析由三处驱动，谁先到都只解析一次（`ensureResolved` 幂等）：

1. `ServerStartedEvent`（留着，能到就到）；
2. `PlayerJoinEvent`（**主要依靠这一条**：进服必然意味着世界在跑，而且这正是服主第一次要看余额的时刻）；
3. 第一次动钱（懒解析，本来就有）。

`/shop status` 仍是**只读**：它不触发解析，也不会因为敲一条查询就在记分板上建目标。

### 19.4 运行时重载不是切配置的捷径

`/ll reload Shop`（LeviLamina `ModManage` 的 `unload` + `load`）会卸载/重载 native DLL、
事件监听与 detour hook。**切配置档仍以停服重启为准**；重载只当可选鲁棒性试验（可能留下旧监听或重复 hook）。
