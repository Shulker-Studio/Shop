// Shop 配色实机预览（LSE / QuickJS）
//
// 目的只有一个：在真实客户端里看颜色。不参与 Shop 的构建，也不读写任何数据。
// 部署：整个目录丢进服务器的 plugins/ShopColorTest/，重启或用 /lse reload。
// 命令：/shoptest
//
// 看什么：
//   1. 聊天栏 16 色的实际观感（基岩版的灰度与 Java 版不同）
//   2. 格式码在基岩版哪些真的生效（删除线、下划线在基岩版是不存在的）
//   3. `§l§6` 与 `§6§l` 的顺序差别（颜色码会清掉它前面的格式码）
//   4. 几套 `[Shop]` 前缀方案
//   5. 语义消息（成功/失败/警告/提示）的对比度
//   6. 表单正文与按钮的配色（按钮背景是客户端画的，彩字会被吃掉）
//   7. Bedrock 独有材质色 §g §h-§v 在这个客户端到底认不认

const C = {
    black: '§0',        darkBlue: '§1',   darkGreen: '§2',  darkAqua: '§3',
    darkRed: '§4',      darkPurple: '§5', gold: '§6',       gray: '§7',
    darkGray: '§8',     blue: '§9',       green: '§a',      aqua: '§b',
    red: '§c',          lightPurple: '§d', yellow: '§e',    white: '§f',
    bold: '§l', italic: '§o', underline: '§n', strike: '§m', obf: '§k', reset: '§r',
};

// 基岩版前景色（与 Java 版不完全相同：§7 是 #C5C5C5，§8 是 #545454）
const COLORS = [
    ['§0', 'black', '#000000'],        ['§1', 'dark_blue', '#0000AA'],
    ['§2', 'dark_green', '#00AA00'],   ['§3', 'dark_aqua', '#00AAAA'],
    ['§4', 'dark_red', '#AA0000'],     ['§5', 'dark_purple', '#AA00AA'],
    ['§6', 'gold', '#FFAA00'],         ['§7', 'gray', '#C5C5C5'],
    ['§8', 'dark_gray', '#545454'],    ['§9', 'blue', '#447FFF'],
    ['§a', 'green', '#54FF54'],        ['§b', 'aqua', '#54FFFF'],
    ['§c', 'red', '#FF5454'],          ['§d', 'light_purple', '#FF54FF'],
    ['§e', 'yellow', '#FFFF54'],       ['§f', 'white', '#FFFFFF'],
];

// 1.21.30+ 的材质色。老客户端上可能不认（表现为无颜色或吞掉后面的字）。
const MATERIALS = [
    ['§g', 'minecoin_gold', '#EFCE16'], ['§h', 'material_quartz', '#D9CCB8'],
    ['§i', 'material_iron', '#A9B4B7'], ['§j', 'material_netherite', '#8F727D'],
    ['§m', 'material_redstone', '#EE222C'], ['§n', 'material_copper', '#C87363'],
    ['§p', 'material_gold', '#FFBF1E'], ['§q', 'material_emerald', '#13A045'],
    ['§s', 'material_diamond', '#5FECFF'], ['§t', 'material_lapis', '#577BFF'],
    ['§u', 'material_amethyst', '#B66CDD'], ['§v', 'material_resin', '#FF6A00'],
];

// ---------------------------------------------------------------- 基础工具

function tell(player, text) {
    // LSE 各版本的方法名不完全一样，挑一个存在的就是了。
    if (typeof player.tell === 'function') {
        return player.tell(text);
    }
    if (typeof player.sendMessage === 'function') {
        return player.sendMessage(text);
    }
    if (typeof player.talkTo === 'function') {
        return player.talkTo(text);
    }
}

function say(player, lines) {
    for (const line of lines) {
        tell(player, line);
    }
}

// 走 RAW 通道发一条：`tellraw` 的 rawtext 是服务端原始文本，客户端会解析 `§`。
// 和上面 `tell` 并排看，就能判断「聊天没颜色」到底是发送通道的问题还是颜色码的问题。
function tellraw(player, text) {
    // 反斜杠与引号要转义才能塞进 JSON；用 String.fromCharCode 取反斜杠，源码里就不出现转义歧义。
    const BS = String.fromCharCode(92);
    const escaped = text.split(BS).join(BS + BS).split('"').join(BS + '"');
    const cmd = 'tellraw "' + player.name + '" {"rawtext":[{"text":"' + escaped + '"}]}';
    if (typeof mc.runcmdEx === 'function') {
        mc.runcmdEx(cmd);
    } else if (typeof mc.runcmd === 'function') {
        mc.runcmd(cmd);
    }
}

function form(title, content) {
    const fm = mc.newSimpleForm();
    fm.setTitle(title);
    if (content) {
        fm.setContent(content);
    }
    return fm;
}

// ---------------------------------------------------------------- 聊天：颜色

function chatColors(player) {
    say(player, [
        `${C.gold}${C.bold}== 16 色（基岩版实测值）==${C.reset}`,
        `${C.darkGray}下面每一行：色块 + 名称 + 基岩版 hex${C.reset}`,
        '',
        ...COLORS.map(([code, name, hex]) => `${code}██████ ${name} ${hex}`),
        '',
        `${C.gold}== 对比度参考（表单深底 #1E1E1E）==${C.reset}`,
        `${C.white}白 16.7:1${C.reset}  ${C.aqua}青 13.6:1${C.reset}  ${C.yellow}黄 15.5:1${C.reset}  ${C.green}绿 12.4:1${C.reset}`,
        `${C.red}红 5.3:1${C.reset}  ${C.gray}灰 9.6:1${C.reset}  ${C.darkGray}深灰 2.2:1 ✗ 正文里别用${C.reset}`,
    ]);
}

// ---------------------------------------------------------------- 聊天：格式码

function chatFormats(player) {
    say(player, [
        `${C.gold}${C.bold}== 格式码在基岩版的真实表现 ==${C.reset}`,
        '',
        `${C.bold}§l 粗体${C.reset} / ${C.italic}§o 斜体${C.reset} / ${C.obf}§k 混淆${C.reset}`,
        `${C.strike}§m 删除线（基岩版没有）${C.reset}`,
        `${C.underline}§n 下划线（基岩版没有）${C.reset}`,
        `${C.gray}注意：§m 与 §n 在基岩版被复用成材质色，见下面那条${C.reset}`,
        '',
        `${C.gold}== 顺序问题：颜色码会清掉它前面的格式码 ==${C.reset}`,
        `${C.bold}${C.gold}这个 §l§6 不粗体，只剩金色${C.reset}`,
        `${C.gold}${C.bold}这个 §6§l 才是金色的粗体${C.reset}`,
        `${C.white}所以永远先颜色、后格式。${C.reset}`,
        '',
        `${C.gold}== 重置 ==${C.reset}`,
        `${C.red}红色${C.bold}红色粗体${C.reset}${C.green}§r 之后回到绿色默认${C.reset}`,
    ]);
}

// ---------------------------------------------------------------- 聊天：前缀方案

function chatPrefix(player) {
    const schemes = [
        ['A 灰底金字（现在用的）', `${C.darkGray}[${C.gold}Shop${C.darkGray}]${C.reset} `],
        ['B 纯金字', `${C.gold}[Shop]${C.reset} `],
        ['C 金字带粗体', `${C.gold}${C.bold}[Shop]${C.reset} `],
        ['D 青色', `${C.aqua}[Shop]${C.reset} `],
        ['E 灰色低调', `${C.gray}[Shop]${C.reset} `],
        ['F 无前缀', ''],
    ];
    const lines = [`${C.gold}${C.bold}== 前缀方案 ==${C.reset}`, ''];
    for (const [name, prefix] of schemes) {
        lines.push(`${C.darkGray}${name}${C.reset}`);
        lines.push(`${prefix}${C.white}买到 64 份 钻石（共 64 个），支付 $ 640${C.reset}`);
        lines.push('');
    }
    say(player, lines);
}

// ---------------------------------------------------------------- 聊天：语义消息

function chatSemantics(player) {
    const P = `${C.darkGray}[${C.gold}Shop${C.darkGray}]${C.reset} `;
    say(player, [
        `${C.gold}${C.bold}== 语义消息（Tone 对照）==${C.reset}`,
        '',
        `${P}${C.green}买入成功：${C.white}钻石 ×64${C.reset} ${C.darkGray}（${C.white}-640${C.darkGray}）${C.reset}`,
        `${P}${C.red}余额不够，差 $ 120${C.reset}`,
        `${P}${C.yellow}你已经不在那家店所在的维度，请回到店里再操作${C.reset}`,
        `${P}${C.gray}这家店只收购，不出售${C.reset}`,
        '',
        `${C.gold}== 同一句话，几种上色方式 ==${C.reset}`,
        `${P}${C.white}买到 64 份 钻石（共 64 个），支付 $ 640${C.reset}`,
        `${P}${C.green}买到 ${C.white}64${C.green} 份 ${C.white}钻石${C.green}（共 ${C.white}64${C.green} 个），支付 ${C.yellow}$ 640${C.reset}`,
        `${P}${C.gray}买到 ${C.white}64${C.gray} 份 ${C.white}钻石${C.gray}（共 ${C.white}64${C.gray} 个），支付 ${C.white}$ 640${C.reset}`,
    ]);
}

// ---------------------------------------------------------------- 聊天：材质色

function chatMaterials(player) {
    say(player, [
        `${C.gold}${C.bold}== Bedrock 材质色 §g §h-§v ==${C.reset}`,
        `${C.gray}如果你的客户端不认这些码，下面会出现无颜色的字、或者少字。${C.reset}`,
        `${C.gray}认的话会显示成对应的材质颜色（比 16 色柔和）。${C.reset}`,
        '',
        ...MATERIALS.map(([code, name, hex]) => `${code}██████ ${name} ${hex}`),
        '',
        `${C.gold}== 材质色 vs 16 色 ==${C.reset}`,
        `${C.yellow}§e yellow #FFFF54${C.reset}   vs   §pmaterial_gold #FFBF1E`,
        `${C.green}§a green #54FF54${C.reset}   vs   §qmaterial_emerald #13A045`,
        `${C.gray}§7 gray #C5C5C5${C.reset}   vs   §imaterial_iron #A9B4B7`,
    ]);
}

// ---------------------------------------------------------------- 表单：正文

function formBody(player) {
    const RULE = '──────';
    const section = (title) => `${C.gray}${RULE} ${C.white}${title}${C.gray} ${RULE}${C.reset}`;
    const body = [
        `${C.aqua}店主§7:  ${C.white}管理员商店`,
        `${C.aqua}商品§7:  ${C.white}Diamond`,
        `${C.aqua}每份§7:  ${C.white}1 个`,
        section('价格'),
        `${C.aqua}买入价§7:  ${C.yellow}$ 1200 / 份`,
        `${C.aqua}卖出价§7:  ${C.yellow}$ 1000 / 份`,
        section('库存与余额'),
        `${C.aqua}可成交份数§7:  ${C.white}无限`,
        `${C.aqua}余额§7:  ${C.green}$ 24667`,
        '',
        `${C.darkGray}── 对比: 不分组、不加线 ──`,
        `${C.aqua}店主§7:  ${C.white}管理员商店`,
        `${C.aqua}买入价§7:  ${C.yellow}$ 1200 / 份`,
        `${C.aqua}可成交份数§7:  ${C.white}无限`,
        `${C.aqua}余额§7:  ${C.green}$ 24667`,
    ].join('\n');

    const fm = form(`${C.gold}${C.bold}管理员商店`, body);
    fm.addButton('管理商店');
    // 按钮只写动作：价格与「一份几件」正文刚列过。
    fm.addButton('购买');
    fm.addButton('出售');
    player.sendForm(fm, (pl, id) => {
        tell(pl, `${C.gray}点了第 ${id + 1} 个按钮${C.reset}`);
    });

}

// ---------------------------------------------------------------- 表单：按钮对比

function formButtons(player) {
    const fm = form(
        `${C.gold}${C.bold}按钮配色对比`,
        [
            `${C.gray}按钮背景是客户端画的：未选中是灰白、选中会变绿。`,
            `${C.gray}所以彩字压在彩底上就会「看不见」——下面直接对比。`,
            '',
            `${C.white}1. 白字（现在采用的）`,
            `${C.green}2. 绿字 —— 悬停变绿后就没对比度了`,
            `${C.red}3. 红字`,
            `${C.yellow}4. 黄字`,
            `${C.gold}${C.bold}5. 金色粗体`,
            `${C.gray}6. 灰字`,
            `${C.green}购买${C.gray}（1 份 64 个 / $ 10）${C.reset}  ← 7. 绿字 + 灰括号`,
            `${C.white}购买${C.darkGray}（1 份 64 个 / $ 10）${C.reset}  ← 8. 白字 + 深灰括号`,
            `${C.white}购买${C.gray}（1 份 64 个 / $ 10）${C.reset}  ← 9. 白字 + 灰括号`,
            `${C.yellow}改买入价（现在 $ 1200 / 份）${C.reset}  ← 10. 黄字`,
            `${C.white}改买入价（现在 $ 1200 / 份）${C.reset}  ← 11. 白字`,
        ].join('\n')
    );
    const labels = [
        `${C.white}1 白字`,
        `${C.green}2 绿字`,
        `${C.red}3 红字`,
        `${C.yellow}4 黄字`,
        `${C.gold}${C.bold}5 金色粗体`,
        `${C.gray}6 灰字`,
        `${C.green}7 购买${C.gray}（1 份 64 个 / $ 10）`,
        `${C.white}8 购买${C.darkGray}（1 份 64 个 / $ 10）`,
        `${C.white}9 购买${C.gray}（1 份 64 个 / $ 10）`,
        `${C.yellow}10 改买入价（现在 $ 1200 / 份）`,
        `${C.white}11 改买入价（现在 $ 1200 / 份）`,
    ];
    for (const label of labels) {
        fm.addButton(label);
    }
    player.sendForm(fm, (pl, id) => {
        tell(pl, `${C.gray}你点的是第 ${id + 1} 个按钮${C.reset}`);
    });
}

// ---------------------------------------------------------------- 表单：全彩

function formAllColors(player) {
    const body = [
        `${C.gold}${C.bold}标题行（§6§l）${C.reset}`,
        '',
        ...COLORS.map(([code, name]) => `${code}████ ${name} 这是这一行的正文示例`),
        '',
        `${C.gray}── 材质色（不认就没颜色）──`,
        ...MATERIALS.map(([code, name]) => `${code}████ ${name}`),
    ].join('\n');
    const fm = form(`${C.gold}${C.bold}全彩正文`, body);
    fm.addButton(`${C.white}关闭`);
    player.sendForm(fm, () => {});
}

// ---------------------------------------------------------------- 聊天：通道对比

function chatChannels(player) {
    const line = `${C.gold}[Shop]${C.reset} ${C.green}买入成功：${C.white}钻石 ×64${C.reset}`;
    say(player, [
        `${C.gold}${C.bold}== 发送通道对比 ==${C.reset}`,
        `${C.gray}下面同一句话发两遍，一遍走 tell、一遍走 tellraw。${C.reset}`,
        `${C.gray}tell 走 TextPacketType::Chat，客户端按纯文本渲染，§ 码会被吃掉。${C.reset}`,
        `${C.gray}tellraw 的 rawtext 走 Raw，§ 码生效。${C.reset}`,
        '',
        `${C.gray}--- tell（预期：没有颜色，且少了几个字）---`,
    ]);
    tell(player, line);
    say(player, ['', `${C.gray}--- tellraw（预期：灰底金字前缀 + 绿字 + 白字）---`]);
    tellraw(player, line);
    say(player, ['', `${C.gray}两边一模一样 = 你的服务端把 Chat 也当 Raw 处理了。${C.reset}`]);
}

// ---------------------------------------------------------------- 菜单

function openMenu(player) {
    const fm = form(
        `${C.gold}${C.bold}Shop 配色测试`,
        [
            `${C.gray}选一项看效果，聊天栏和表单会一起发出来。`,
            `${C.gray}截图的时候最好一屏能装下，方便对比。`,
        ].join('\n')
    );
    const actions = [
        [`${C.white}聊天：16 色`, chatColors],
        [`${C.white}聊天：格式码与顺序`, chatFormats],
        [`${C.white}聊天：前缀方案`, chatPrefix],
        [`${C.white}聊天：语义消息`, chatSemantics],
        [`${C.yellow}聊天：材质色 §g-§v`, chatMaterials],
        [`${C.yellow}聊天：发送通道对比（tell vs tellraw）`, chatChannels],
        [`${C.white}表单：正文配色`, formBody],
        [`${C.white}表单：按钮对比`, formButtons],
        [`${C.white}表单：全彩正文`, formAllColors],
    ];
    for (const [label] of actions) {
        fm.addButton(label);
    }
    player.sendForm(fm, (pl, id) => {
        const action = actions[id];
        if (action) {
            action[1](pl);
        }
    });
}

// ---------------------------------------------------------------- 注册

mc.regPlayerCmd('shoptest', 'Shop 配色测试（聊天 + 表单）', (player, args) => {
    const sub = (args && args[0] || '').toLowerCase();
    switch (sub) {
        case 'chat':      return chatColors(player);
        case 'format':    return chatFormats(player);
        case 'prefix':    return chatPrefix(player);
        case 'semantic':  return chatSemantics(player);
        case 'material':  return chatMaterials(player);
        case 'channel':   return chatChannels(player);
        case 'form':      return formBody(player);
        case 'button':    return formButtons(player);
        case 'all':       return formAllColors(player);
        default:          return openMenu(player);
    }
}, 0);

logger.info('[ShopColorTest] /shoptest 已注册：chat / format / prefix / semantic / material / channel / form / button / all');
