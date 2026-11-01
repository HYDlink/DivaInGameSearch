// 与 page_probe.js 在同一脚本上下文加载；默认无 Hook、无调用，仅在显式命令后动作。
let routeListener = null;
let routeTimer = null;
let routeEvents = [];
let routeArmed = false;
let routeWatchThread = null;

// 参数 kind、data 为事件和数据；有界记录，供宿主拉取，避免高频消息淹没协议。
function routeLog(kind, data) {
    if (routeEvents.length < 120) routeEvents.push({timestamp: new Date().toISOString(), kind, ...data});
}
// 清理本脚本唯一拦截器和计时器，不撤销其他 Mod 的 Hook。
function routeCancel() {
    routeArmed = false;
    if (routeWatchThread) {
        routeWatchThread.unsetHardwareWatchpoint(3);
        routeWatchThread = null;
    }
    if (routeListener) routeListener.detach();
    if (routeTimer) clearTimeout(routeTimer);
    routeListener = null;
    routeTimer = null;
    return {取消: true};
}
// 参数 settings 包含已知入口 RVA；只返回有界反汇编，不执行代码。
function routeCode(settings) {
    if (settings.menuRegion) {
        const rows = [];
        let cursor = imageBase.add(settings.menuRegion === 'frame' ? 0x2C4A20 : 0x2C4B80);
        for (let count = 0; count < 160 && cursor.compare(imageBase.add(0x2C4D00)) < 0; count++) {
            const instruction = Instruction.parse(cursor);
            rows.push({rva: cursor.sub(imageBase).toString(), instruction: instruction.toString()});
            cursor = instruction.next;
        }
        return rows;
    }
    if (settings.returnSite) {
        if (![0x6F079C, 0x1FA62854, 0x1FA62874, 0x1FA6289A, 0x2C4AAA, 0xEC4986D].includes(settings.returnSite))
            throw new Error('不是本轮已观察到的返回站点');
        const address = imageBase.add(settings.returnSite).sub(5);
        const instruction = Instruction.parse(address);
        if (settings.continuation) {
            const rows = [];
            let cursor = imageBase.add(settings.returnSite);
            for (let index = 0; index < 22; index++) {
                const next = Instruction.parse(cursor);
                rows.push({address: cursor.toString(), instruction: next.toString()});
                cursor = next.next;
                if (['ret', 'jmp', 'int3'].includes(next.mnemonic)) break;
            }
            return {call: instruction.toString(), continuation: rows};
        }
        return {address: address.toString(), instruction: instruction.toString(),
            directTarget: instruction.operands[0]?.value || null};
    }
    const allowed = [0x69B040, 0x69B5E0, 0x69BA10, 0x69A100, 0x6968D0];
    if (!allowed.includes(settings.rva)) throw new Error('不在当前静态候选清单');
    const result = [];
    let address = imageBase.add(settings.rva);
    for (let index = 0; index < 12; index++) {
        let instruction;
        try { instruction = Instruction.parse(address); }
        catch (error) { result.push({address: address.toString(), 未解码: String(error)}); break; }
        result.push({address: address.toString(), instruction: instruction.toString()});
        address = instruction.next;
        if (['ret', 'jmp', 'int3'].includes(instruction.mnemonic)) break;
    }
    return result;
}
// 自然 Enter 确认动画入口的单 Hook 观察，最多55秒；不写字段、不调用原生函数。
function routeObserve(settings) {
    routeCancel();
    routeEvents = [];
    const rva = settings.rva === 0x69BA10 ? 0x69BA10 : 0x69B040;
    routeListener = Interceptor.attach(imageBase.add(rva), {
        // 参数 values 是真实原生调用寄存器参数，只记录当前有效父页。
        onEnter(values) {
            const current = snapshot();
            const owner = rva === 0x69BA10 ? read(values[0].add(8)) : values[0];
            this.routeMatch = current.page === '自定义父页' && owner && owner.toString() === current.scene;
            if (!this.routeMatch) return;
            routeLog('自然确认入口', {thread: Process.getCurrentThreadId(), argument: values[0].toString(),
                details: details(), returnAddress: this.returnAddress.toString()});
        },
        // 正常返回后仅保存快照，调用者的后续状态写入由宿主采样。
        onLeave() { if (this.routeMatch) routeLog('自然确认返回', {details: details()}); }
    });
    routeTimer = setTimeout(routeCancel, 55000);
    return {观察: '父页自然确认', rva: '0x' + rva.toString(16), 写内存: false, 原生调用: false};
}
// 参数 settings 指明父页目标；只在实际内部状态帧返回边界调用一次已观测的确认入口。
function routeArmParent(settings) {
    routeCancel();
    routeEvents = [];
    const current = details();
    if (current.page !== '自定义父页' || current.transition !== 1 || current.target !== current.head
        || current.parentCursor !== settings.cursor || current.sceneFields['0x1c'] !== 1)
        throw new Error('当前父页未满足确认前置条件');
    const state = current.internalState.rows.find(row => row.key === 0);
    if (!state || state.vtable !== '0xcc51f8') throw new Error('父页主状态对象不匹配');
    const statePointer = ptr(state.object), owner = ptr(current.scene);
    if (!read(statePointer.add(8)).equals(owner)) throw new Error('状态对象不属于当前父页');
    const confirm = new NativeFunction(imageBase.add(0x69BA10), 'void', ['pointer']);
    const startAnimation = new NativeFunction(imageBase.add(0x69B040), 'void', ['pointer']);
    let boundaryCalls = 0;
    routeArmed = true;
    routeListener = Interceptor.attach(imageBase.add(0x66B2D0), {
        // 参数 values 为真实状态更新调用，仅匹配当前父页持有的内部状态机。
        onEnter(values) {
            this.routeEligible = values[0].toString() === current.internalState.manager;
            if (++boundaryCalls <= 2) routeLog('状态边界命中', {argument: values[0].toString(), eligible: this.routeEligible});
        },
        // 在原状态更新完成后确认，原生 SceneManager 随后消费完成标记，不直接写菜单字段。
        onLeave() {
            if (!routeArmed || !this.routeEligible) return;
            const fresh = snapshot();
            if (fresh.page !== '自定义父页' || fresh.scene !== current.scene || fresh.controller !== current.controller
                || fresh.parentCursor !== settings.cursor || fresh.target !== fresh.head) {
                routeArmed = false;
                routeLog('受控确认取消', {reason: '上下文改变', snapshot: fresh});
                return;
            }
            routeArmed = false;
            routeLog('受控确认前', {thread: Process.getCurrentThreadId(), snapshot: fresh, state: state.object});
            try {
                if (settings.animate) {
                    if (read(owner.add(0x1C), 'int') !== 1) throw new Error('确认子状态改变');
                    startAnimation(owner);
                    // 精确复现已静态及自然调用确认的 Enter 分支；不是写全局菜单或场景目标。
                    owner.add(0x1C).writeS32(2);
                } else confirm(statePointer);
                routeLog('受控确认返回', {snapshot: snapshot()});
            } catch (error) { routeLog('受控确认异常', {error: String(error)}); }
        }
    });
    routeTimer = setTimeout(routeCancel, 12000);
    return {已排队: true, rva: settings.animate ? '0x69B040+父页确认子状态' : '0x69BA10', state: state.object, cursor: settings.cursor};
}
// 拉取累计事件并清空，避免重复日志。
function routeDrain() { const result = routeEvents; routeEvents = []; return result; }
// 参数 settings 包含已认证的目标服装ID，在真实模型状态帧末复用既有原生控件定位协议。
function routeArmLocate(settings) {
    routeCancel(); routeEvents = [];
    const current = details();
    if (current.page !== '模型' || current.target !== current.head || current.transition !== 1
        || current.sceneFields['0x60'] !== -1 || ![1, 2].includes(current.sceneFields['0x20']))
        throw new Error('当前不是可定位的正常模型列表');
    const scene = ptr(current.scene), begin = ptr(current.module.begin);
    const matches = [];
    for (let index = 0; index < current.module.count; index++) {
        const record = read(begin.add(index * 8));
        if (read(record, 'int') === settings.id) matches.push(index);
    }
    if (matches.length !== 1) throw new Error('目标服装未在当前真实列表中唯一匹配');
    const initialize = new NativeFunction(imageBase.add(0x6916E0), 'void', ['pointer', 'uint8']);
    routeArmed = true;
    routeListener = Interceptor.attach(imageBase.add(0x66B2D0), {
        // 参数 values 为内部状态更新器，匹配当前模型状态机。
        onEnter(values) { this.routeEligible = values[0].toString() === current.internalState.manager; },
        // 同帧暂存并恢复索引缓存，让原生控件完整更新高亮与轮播。
        onLeave() {
            if (!routeArmed || !this.routeEligible) return;
            routeArmed = false;
            const fresh = snapshot();
            if (fresh.page !== '模型' || fresh.scene !== current.scene || fresh.controller !== current.controller
                || fresh.module.begin !== current.module.begin || fresh.module.count !== current.module.count)
                return routeLog('定位取消', {snapshot: fresh});
            const saved = scene.add(0x30).readS32();
            try {
                scene.add(0x30).writeS32(matches[0]); initialize(scene, 1);
                routeLog('原生控件定位', {id: settings.id, index: matches[0], thread: Process.getCurrentThreadId()});
            } finally { scene.add(0x30).writeS32(saved); }
        }
    });
    routeTimer = setTimeout(routeCancel, 10000);
    return {目标ID: settings.id, 索引: matches[0]};
}
// 仅观察当前模型内部状态切换，过滤其他管理器；记录预览与加载键，不主动切状态。
function routeObserveModule() {
    routeCancel(); routeEvents = [];
    const current = details();
    if (current.page !== '模型') throw new Error('需要当前模型页');
    routeListener = Interceptor.attach(imageBase.add(0x66B3A0), {
        // 参数 values 为状态机和目标状态；只记录当前模型页的状态机。
        onEnter(values) {
            this.routeMatch = values[0].toString() === current.internalState.manager;
            if (!this.routeMatch) return;
            const scene = ptr(current.scene), controller = ptr(current.controller);
            routeLog('模型状态切换', {thread: Process.getCurrentThreadId(), target: values[1].toInt32(),
                scene: current.scene, menuIndex: read(scene.add(0x60), 'int'),
                menuTarget: read(read(scene.add(0x68)), 'int'), nextState: read(scene.add(0x14), 'int'),
                operation: read(scene.add(0x18), 'int'), phase: read(scene.add(0x1C), 'int'),
                field320: read(controller.add(0x320), 'int'), field338: read(controller.add(0x338), 'int')});
        }
    });
    routeTimer = setTimeout(routeCancel, 55000);
    return {只读观察: true, rva: '0x66B3A0'};
}
// 只观察已绑定的模型操作回调，确认命令编号和前后字段，不执行任何操作。
function routeObserveAction() {
    routeCancel(); routeEvents = [];
    const current = details();
    if (current.page !== '模型' || current.actionCallback?.boundValues[0] !== current.scene
        || current.actionCallback?.boundValues[1] !== imageBase.add(0x695250).toString())
        throw new Error('原生操作回调绑定不匹配');
    routeListener = Interceptor.attach(imageBase.add(0x695250), {
        // 参数 values 为模型页与原生命令枚举，仅匹配当前已绑定页面。
        onEnter(values) {
            this.routeMatch = values[0].toString() === current.scene;
            if (!this.routeMatch) return;
            this.routeCommand = values[1].toInt32();
            routeLog('操作回调进入', {command: this.routeCommand, scene: current.scene,
                target: read(read(values[0].add(0x68)), 'int'), index: read(values[0].add(0x60), 'int'),
                thread: Process.getCurrentThreadId()});
        },
        // 原函数返回后记录结果字段，不把回调返回当操作完成。
        onLeave() {
            if (!this.routeMatch) return;
            const scene = ptr(current.scene);
            routeLog('操作回调返回', {command: this.routeCommand, next: read(scene.add(0x14), 'int'),
                operation: read(scene.add(0x18), 'int'), phase: read(scene.add(0x1C), 'int')});
        }
    });
    routeTimer = setTimeout(routeCancel, 55000);
    return {只读观察: true, rva: '0x695250'};
}
// 参数 settings 含目标稳定ID和 preview/equip；仅复现本轮已确认拥有的0与316，不改变列表高亮。
function routeArmAction(settings) {
    routeCancel(); routeEvents = [];
    if (![0, 316].includes(settings.id) || !['preview', 'equip'].includes(settings.action))
        throw new Error('本轮只允许已确认拥有的0与316预览或更换');
    const current = details();
    if (current.page !== '模型' || current.transition !== 1 || current.target !== current.head
        || current.sceneFields['0x60'] !== -1 || current.internalState.words[2] !== 0
        || current.actionCallback?.boundValues[0] !== current.scene
        || current.actionCallback?.boundValues[1] !== imageBase.add(0x695250).toString())
        throw new Error('模型操作上下文未通过');
    const scene = ptr(current.scene), begin = ptr(current.module.begin), matches = [];
    for (let index = 0; index < current.module.count; index++) {
        const record = read(begin.add(index * 8));
        if (read(record, 'int') === settings.id) matches.push({index, record});
    }
    if (matches.length !== 1) throw new Error('目标未在当前原生列表中唯一匹配');
    const target = matches[0];
    const command = settings.action === 'preview' ? 7 : 3;
    const callback = new NativeFunction(imageBase.add(0x695250), 'void', ['pointer', 'int']);
    const switchState = new NativeFunction(imageBase.add(0x66B3A0), 'uint8', ['pointer', 'int']);
    routeArmed = true;
    routeListener = Interceptor.attach(imageBase.add(0x66B2D0), {
        // 参数 values 为真实内部状态帧，仅匹配当前模型页。
        onEnter(values) { this.routeEligible = values[0].toString() === current.internalState.manager; },
        // 暂存原生动作目标，调用已观察命令及原生加载状态；保持+0x110高亮索引不变。
        onLeave() {
            if (!routeArmed || !this.routeEligible) return;
            routeArmed = false;
            const fresh = snapshot();
            if (fresh.scene !== current.scene || fresh.controller !== current.controller || fresh.page !== '模型'
                || fresh.module.begin !== current.module.begin || fresh.module.index !== current.module.index
                || read(scene.add(0x60), 'int') !== -1)
                return routeLog('原生动作取消', {snapshot: fresh});
            const oldIndex = scene.add(0x60).readS32(), oldRecord = scene.add(0x68).readPointer();
            try {
                scene.add(0x60).writeS32(target.index); scene.add(0x68).writePointer(target.record);
                routeLog('直接目标动作开始', {action: settings.action, id: settings.id, index: target.index,
                    highlight: fresh.module, command, thread: Process.getCurrentThreadId()});
                callback(scene, command);
                if (scene.add(0x14).readS32() !== 3) throw new Error('原生回调没有认可加载状态');
                switchState(ptr(current.internalState.manager), 3);
                routeLog('直接目标已提交', {snapshot: snapshot()});
            } catch (error) {
                // 仅在仍未切入加载且页面身份不变时撤销本次暂存目标，不覆盖后续原生加载。
                if (read(ptr(current.internalState.manager).add(0x18), 'int') === 0) {
                    scene.add(0x60).writeS32(oldIndex); scene.add(0x68).writePointer(oldRecord);
                }
                routeLog('直接目标动作失败', {error: String(error)});
            }
        }
    });
    routeTimer = setTimeout(routeCancel, 10000);
    return {动作已排队: settings.action, 目标ID: settings.id, 保持高亮ID: current.module.id, 命令: command};
}
// 观察同层F3的场景管理器请求与调用者，单Hook且只读。
function routeObserveSibling() {
    routeCancel(); routeEvents = [];
    routeListener = Interceptor.attach(imageBase.add(0x6968D0), {
        // 参数 values 为实际场景管理器和目标键，记录真实调用者以缩小静态入口。
        onEnter(values) {
            const current = snapshot();
            this.routeMatch = values[0].toString() === current.manager;
            if (!this.routeMatch) return;
            routeLog('场景切换请求', {target: values[1].toInt32(), snapshot: current,
                returnAddress: this.returnAddress.toString(), thread: Process.getCurrentThreadId(),
                trace: Thread.backtrace(this.context, Backtracer.ACCURATE).slice(0, 6).map(String)});
        },
        // 返回只读记录目标节点，跨帧结果由宿主断言。
        onLeave() { if (this.routeMatch) routeLog('场景请求返回', {snapshot: snapshot()}); }
    });
    routeTimer = setTimeout(routeCancel, 55000);
    return {只读观察: true, rva: '0x6968D0'};
}
// 参数 settings.target 为场景键；复现已观测的同层请求和该页面专属完成标记。
function routeArmNavigation(settings) {
    routeCancel(); routeEvents = [];
    const current = details();
    if (![2, 3, 4].includes(current.sceneKey) || ![1, 2, 3, 4].includes(settings.target)
        || current.sceneKey === settings.target || current.transition !== 1 || current.target !== current.head
        || current.internalState.words[2] !== 0)
        throw new Error('同层路由来源、目标或内部状态不符');
    if (current.sceneKey === 2 && current.sceneFields['0x60'] !== -1) throw new Error('模型操作菜单尚未关闭');
    const profile = {2: {done: 0x25, panel: 0x310}, 3: {done: 0x25, panel: 0x380}, 4: {done: 0x29, panel: 0x2E0}}[current.sceneKey];
    const scene = ptr(current.scene), manager = ptr(current.manager);
    const requestScene = new NativeFunction(imageBase.add(0x6968D0), 'uint8', ['pointer', 'int']);
    const hidePanel = new NativeFunction(imageBase.add(0x682530), 'void', ['pointer']);
    routeArmed = true;
    routeListener = Interceptor.attach(imageBase.add(0x66B2D0), {
        // 参数 values 是当前状态帧的状态机对象。
        onEnter(values) { this.routeEligible = values[0].toString() === current.internalState.manager; },
        // 与F3或Esc自然分支一致：请求目标、隐藏当前列表面板、置该场景完成字节。
        onLeave() {
            if (!routeArmed || !this.routeEligible) return;
            routeArmed = false;
            const fresh = snapshot();
            if (fresh.scene !== current.scene || fresh.controller !== current.controller || fresh.target !== fresh.head)
                return routeLog('同层路由取消', {snapshot: fresh});
            const accepted = requestScene(manager, settings.target);
            if (!accepted) return routeLog('同层目标未被接受', {target: settings.target});
            const panel = read(scene.add(profile.panel));
            if (panel && !panel.isNull()) hidePanel(panel);
            scene.add(profile.done).writeU8(1);
            routeLog('同层原生路由提交', {source: current.sceneKey, target: settings.target,
                doneOffset: profile.done, thread: Process.getCurrentThreadId(), snapshot: snapshot()});
        }
    });
    routeTimer = setTimeout(routeCancel, 10000);
    return {路由已排队: true, source: current.sceneKey, target: settings.target};
}
// 只观察已知自定义任务获取器的有限不同调用者，忽略每帧重复的就绪查询。
function routeObserveOuter() {
    routeCancel(); routeEvents = [];
    const seen = new Set();
    routeListener = Interceptor.attach(imageBase.add(0x687B80), {
        // 只记录首次出现的调用地址，最多16处，不追踪所有任务或扩大扫描。
        onEnter() {
            const caller = this.returnAddress.toString();
            if (seen.has(caller) || seen.size >= 16) return;
            seen.add(caller);
            routeLog('外层任务获取调用者', {caller, snapshot: snapshot(), thread: Process.getCurrentThreadId(),
                trace: Thread.backtrace(this.context, Backtracer.ACCURATE).slice(0, 7).map(String)});
        }
    });
    routeTimer = setTimeout(routeCancel, 55000);
    return {只读观察: true, rva: '0x687B80', 去重上限: 16};
}
// 只观察已由R实际调用的外层激活接口参数，最多记录八次。
function routeObserveActivation() {
    routeCancel(); routeEvents = [];
    let count = 0;
    routeListener = Interceptor.attach(imageBase.add(0x1DE750), {
        // 参数 values 为曲目ID和可空上下文指针；只读记录，不更改。
        onEnter(values) {
            this.routeMatch = ++count <= 8;
            if (!this.routeMatch) return;
            routeLog('外层激活入口', {pvIdArgument: values[0].toInt32(), contextArgument: values[1].toString(),
                caller: this.returnAddress.toString(), thread: Process.getCurrentThreadId(), snapshot: snapshot()});
        },
        // 返回后读取已知任务对象的来源参数，不能把返回成功当页面就绪。
        onLeave() { if (this.routeMatch) routeLog('外层激活返回', {outer: outerInfo()}); }
    });
    routeTimer = setTimeout(routeCancel, 55000);
    return {只读观察: true, rva: '0x1DE750'};
}
// 在选曲真实帧末复现已静态及自然调用确认的R分支，不发送R或写全局菜单。
function routeArmActivateSong() {
    routeCancel(); routeEvents = [];
    const initial = snapshot();
    if (initial.page !== '歌曲' || initial.songState !== 6 || initial.controller !== '0x0')
        throw new Error('原生激活只接受无自定义任务的稳定选曲页');
    const activate = new NativeFunction(imageBase.add(0x1DE750), 'uint8', ['int', 'pointer']);
    const hide = new NativeFunction(imageBase.add(0x6EECD0), 'void', ['pointer', 'uint8']);
    routeArmed = true;
    routeListener = Interceptor.attach(imageBase.add(0x6EDC40), {
        // 参数 values[0] 必须等于本次从全局读取的真实选曲对象。
        onEnter(values) { this.routeEligible = values[0].toString() === initial.song; },
        // 再次校验来源与曲目，随后依次调用自然R分支的两个原生入口。
        onLeave() {
            if (!routeArmed || !this.routeEligible) return;
            routeArmed = false;
            const fresh = snapshot();
            if (fresh.page !== '歌曲' || fresh.song !== initial.song || fresh.pvid !== initial.pvid)
                return routeLog('选曲原生激活取消', {snapshot: fresh});
            try {
                const accepted = activate(fresh.pvid, ptr(0));
                if (!accepted) throw new Error('任务激活未受理');
                hide(ptr(fresh.song), 1);
                routeLog('选曲原生激活已提交', {pvid: fresh.pvid, thread: Process.getCurrentThreadId(), snapshot: snapshot()});
            } catch (error) { routeLog('选曲原生激活失败', {error: String(error)}); }
        }
    });
    routeTimer = setTimeout(routeCancel, 10000);
    return {原生选曲激活已排队: true, pvid: initial.pvid};
}
// 在真实父页状态帧末复现Esc的Finish请求；不直接删除任务或改全局菜单。
function routeArmFinish() {
    routeCancel(); routeEvents = [];
    const current = details();
    if (current.page !== '自定义父页' || current.transition !== 1 || current.target !== current.head
        || ![5, 36].includes(current.menu[0]) || read(ptr(current.scene).add(0x1C), 'int') !== 1)
        throw new Error('退出只接受已认证来源的稳定父页');
    const requestScene = new NativeFunction(imageBase.add(0x6968D0), 'uint8', ['pointer', 'int']);
    routeArmed = true;
    routeListener = Interceptor.attach(imageBase.add(0x66B2D0), {
        // 参数 values 为真实状态机，必须匹配本次父页持有的对象。
        onEnter(values) { this.routeEligible = values[0].toString() === current.internalState.manager; },
        // 请求原生Finish场景，并设置自然Esc分支确认过的退出原因与完成字节。
        onLeave() {
            if (!routeArmed || !this.routeEligible) return;
            routeArmed = false;
            const fresh = snapshot();
            if (fresh.page !== '自定义父页' || fresh.scene !== current.scene || fresh.controller !== current.controller
                || fresh.target !== fresh.head) return routeLog('原生退出取消', {snapshot: fresh});
            try {
                if (!requestScene(ptr(current.manager), 8)) throw new Error('Finish场景未受理');
                ptr(current.controller).add(0x28).writeS32(1);
                ptr(current.scene).add(0x19).writeU8(1);
                routeLog('原生退出已提交', {sourceMenu: current.menu, thread: Process.getCurrentThreadId(), snapshot: snapshot()});
            } catch (error) { routeLog('原生退出失败', {error: String(error)}); }
        }
    });
    routeTimer = setTimeout(routeCancel, 10000);
    return {原生退出已排队: true, 来源: current.menu[0] === 5 ? '歌曲' : '主页'};
}
// 参数 settings.thread 为本进程自然调用实际记录的线程，只观察一个已知菜单字段的一次变化。
function routeObserveMenuWriter(settings) {
    routeCancel(); routeEvents = [];
    const initial = snapshot();
    if (initial.page !== '主页') throw new Error('菜单写入观察只允许稳定主页');
    const thread = Process.enumerateThreads().find(item => item.id === settings.thread);
    if (!thread) throw new Error('本次实测游戏线程已不存在');
    const watched = imageBase.add(0xCC61098), before = watched.readS32();
    // 参数 exception 为原生异常；只消费本观察点在线程内触发且字段已改变的单步异常。
    Process.setExceptionHandler(function(exception) {
        if (!routeWatchThread || exception.type !== 'single-step' || Process.getCurrentThreadId() !== thread.id
            || watched.readS32() === before) return false;
        routeWatchThread.unsetHardwareWatchpoint(3);
        routeWatchThread = null;
        const context = exception.context;
        routeLog('外层菜单写入者', {thread: thread.id, before, after: watched.readS32(),
            address: exception.address.toString(), pc: context.pc.toString(),
            registers: {rcx: String(context.rcx), rdx: String(context.rdx), r8: String(context.r8), r9: String(context.r9)},
            trace: Thread.backtrace(context, Backtracer.ACCURATE).slice(0, 10).map(String)});
        return true;
    });
    routeWatchThread = thread;
    try { thread.setHardwareWatchpoint(3, watched, 4, 'w'); }
    catch (error) { routeWatchThread = null; throw error; }
    routeTimer = setTimeout(routeCancel, 15000);
    return {单次写入观察: true, 字段: '0xCC61098', thread: thread.id, 最长秒数: 15};
}
// 单Hook观察已捕获外层路由入口的模式和页面参数；不主动调用。
function routeObserveOuterRoute(settings) {
    routeCancel(); routeEvents = [];
    let count = 0;
    const rva = settings.rva === 0x2C4BB0 ? 0x2C4BB0 : 0x2C48B0;
    routeListener = Interceptor.attach(imageBase.add(rva), {
        // 参数 values 为静态确认的模式与页面键，记录最多八次。
        onEnter(values) {
            this.routeMatch = ++count <= 8;
            if (this.routeMatch) routeLog('外层路由自然调用', {mode: values[0].toInt32(), page: rva === 0x2C48B0 ? values[1].toInt32() : null,
                caller: this.returnAddress.toString(), thread: Process.getCurrentThreadId(), outer: outerInfo()});
        },
        // 返回只读记录路由结构，不以返回等同页面完成。
        onLeave() { if (this.routeMatch) routeLog('外层路由自然返回', {outer: outerInfo()}); }
    });
    routeTimer = setTimeout(routeCancel, 15000);
    return {只读观察: true, rva: '0x' + rva.toString(16)};
}
// 参数 settings.mode 仅允许主页自然入口已确认的选曲2或自定义7。
function routeArmHome(settings) {
    routeCancel(); routeEvents = [];
    const current = outerInfo();
    if (current.snapshot.page !== '主页' || ![2, 7].includes(settings.mode)
        || current.menuEngine[0] !== 9 || current.menuEngine[3] !== 1
        || (current.menuEngine[4] & 0xffff) !== 0 || (current.menuEngine[11] & 0xffff) !== 0)
        throw new Error('主页模式或切换标记不满足原生请求条件');
    const shiftMode = new NativeFunction(imageBase.add(0x2C4BB0), 'void', ['int']);
    routeArmed = true;
    routeListener = Interceptor.attach(imageBase.add(0x2C4A40), {
        // 在外层模式控制帧返回后提交正式请求，让下一帧正常执行销毁及初始化。
        onLeave() {
            if (!routeArmed) return;
            routeArmed = false;
            const fresh = outerInfo();
            if (fresh.snapshot.page !== '主页' || fresh.menuEngine[0] !== 9
                || (fresh.menuEngine[4] & 0xffff) !== 0 || (fresh.menuEngine[11] & 0xffff) !== 0)
                return routeLog('主页原生请求取消', {outer: fresh});
            try {
                shiftMode(settings.mode);
                routeLog('主页原生模式请求', {mode: settings.mode, thread: Process.getCurrentThreadId(), outer: outerInfo()});
            } catch (error) { routeLog('主页原生请求失败', {error: String(error)}); }
        }
    });
    routeTimer = setTimeout(routeCancel, 10000);
    return {主页原生请求已排队: true, 目标模式: settings.mode};
}
rpc.exports = {...rpc.exports, armhome: routeArmHome, observeouterroute: routeObserveOuterRoute, observemenuwriter: routeObserveMenuWriter, armfinish: routeArmFinish, armactivatesong: routeArmActivateSong, observe: routeObserve, events: routeDrain, cancel: routeCancel, code: routeCode,
    armparent: routeArmParent, armlocate: routeArmLocate, observemodule: routeObserveModule,
    observeaction: routeObserveAction, armaction: routeArmAction, observesibling: routeObserveSibling,
    armnavigation: routeArmNavigation, observeouter: routeObserveOuter, observeactivation: routeObserveActivation};
