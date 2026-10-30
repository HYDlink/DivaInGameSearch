> **当前交接入口：[CURRENT_RUNTIME_HANDOFF.md](CURRENT_RUNTIME_HANDOFF.md)。1.0.6已部署且用户确认服装定位成功。下文含历史阶段记录；对象必须以真实模型帧this及生产列表判据验证，不能把虚表扫描候选当活跃页面。**

# 原生服装定位研究与实施交接

日期：2026-10-29。**请先阅读 [完整实验报告](CUSTOMIZATION_EXPERIMENT_REPORT.md)，它是当前规划的统一结论，优先于各轮历史阶段记录。**

## 当前可以交付的能力

已通过 Frida 探针在实际模型帧线程调用原生函数：

- 同分类：MIKU ID316 → ID394 → ID316，名称、中央卡片、高亮及列表读回一致。
- 跨分类：RIN ID40 → MIKU 非推荐 ID394 → RIN ID40，角色分类、卡片、名称和真实列表 ID 均核对。
- 分类函数 RVA 0x694D00(page,int32 direction)，实际验证相邻分类±1；控件函数 RVA 0x6916E0(page,uint8 flag)，flag=1。
- 页面 +0x70／+0x78 为当前有效列表边界，元素为记录指针，记录首 int32 与已知 Module ID 对应；必须按当前列表唯一查目标。
- 定位暂存页面 +0x30、写目标索引、同帧调用控件函数并 finally 恢复缓存。直接写 +0x110 会造成卡片不同步，已排除。
- 控制器 +0x40 是 UI 分类字段，0/1/2 对应 MIKU/RIN/LEN 的操作对照；不是元数据角色枚举或歌者槽位。

这些结果足够开始限定范围的最小 C++ 集成。搜索 DLL 尚未实现或部署原生服装定位，线上 1.0.0 没有因此改变；不能把探针通过写成 DLL 验收通过。

## 必须完成的实现工作

1. 复用现有 customization* 与搜索 UI，不重建界面。实现前核对工作树和仍活跃的歌曲实施工作，避免同时修改共享业务文件；不 checkout/reset/stash/清理或覆盖用户修改。
2. 按已验证主程序哈希、入口指令、虚表和对象关联建立 profile；本机 PID、9972 线程号和堆地址只作实验记录，不可硬编码。
3. 在模型原生帧 RVA 0x6910B0 获得实际 this，并处理离页、原地重建、换槽位和设置范围变化。旧 manager+0x1A8 获取公式在新进程失效，禁止复用。
4. 请求携带稳定 ID、kind、人物／UI 分类映射、槽位与设置范围上下文、contextGeneration、catalogFingerprint、serial。上下文未知时拒绝，不伪装默认值为读取成功。
5. 分类变化后重新读取列表和映射；原生拒绝切分类或目标不在列表时明确失败，不强写分类／装备绕过。暂仅启用 MIKU/RIN 经测试路径。
6. 在游戏帧线程执行原生操作，保留其他 Mod hook 链；不在 DllMain 初始化，不在帧回调阻塞，不从后台线程直接调用。
7. 跨帧核对实际分类、选中 ID、列表和 UI。Native 返回值不是已验证成功码。定位不自动试穿／装备，玩家保留更换、购买和保存确认权。
8. 补 C++ 插件的实机回归，再逐步扩展其他分类、槽位、共通设置、过滤、未购买／隐藏／锁定及高 ID。发型与饰品另行研究。

已验证正常路径不等于完整副作用证明；本研究没有全面跟踪所有存档／装备写者。实验中的定时恢复不应直接成为产品成功后的自动跳回行为；失败恢复必须尊重玩家的新选择。

## 资料与并发规则

所有命令显式在 DivaInGameSearch 目录执行。先读 AGENTS.md、docs/IMPLEMENTATION_HANDOFF.md、CLOUD_PATCH_REVIEW、CUSTOMIZATION_IMPORT、CUSTOMIZATION_SEARCH_PLAN 及完整实验报告。中文沟通／注释／控制台提示，UTF-8，新增方法和参数中文说明。

原始探针、Evidence 与截图在 artifacts/customization-research，默认被 Git 忽略，不会自动随源码传递。交给其他环境时使用本次完整本地交接包；未包含游戏可执行文件或模型／纹理资源。历史探针含进程特定守卫，不能未经重新核查直接执行。

一次仅一个实验拥有游戏动态操作权。其他任务可并行做静态源码／元数据分析，实际 Frida 采样和菜单操作按批次交接。后续每轮先说明目标、证据、假设，同一 Session 批量采集并过滤无效高频事件；结束报告确认字段、候选 RVA、排除项和未知，导出 Evidence 后分离。观察 hook 也是进程插桩。

截图按 docs/GAME_SCREENSHOT_GUIDE.md 使用现有 NativeCapture，必要时按宿主机制取得交互桌面执行权限；打开图片核对并记录 PrintWindow 或 GDI 分支，不称为 D3D。自动按键本轮未被游戏消费，不能把 API 成功当作操作成功。

详细受控流程与证据见 [第04轮](CUSTOMIZATION_CONTROLLED_LOCATE_04.md)、[第05轮](CUSTOMIZATION_CROSS_CATEGORY_05.md)。其他领域不得直接套用 Module 地址或 ABI。
