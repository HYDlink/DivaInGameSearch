# 模型服装、发型与饰品搜索研究

这是后续研究，1.0.0 尚未实现这些功能。“模型”若指服装 Module，可沿原生定制菜单研究；若指底层渲染资源，还需另找资源与预览生命周期。

## 已有依据

游戏 Module 表包含 id、name、chara、cos、sort_index；定制物品表包含人物、parts、obj_id、bind_module。Mod 的 mod_str_array.toml 可补充多语言名称。必须合并当前生效来源，表内序号不能替代 Module ID。

[ReDIVA](https://github.com/korenkonder/ReDIVA) 的 module_table、customize_item_table、玩家装备结构可帮助理解格式与上下文；[DivaModLoader](https://github.com/blueskythlikesclouds/DivaModLoader) 的 StrArray、DatabaseLoader、SaveData 可帮助寻找 PC 名称和数据路径。这些源码不构成已验证的商业 PC 原生 SelectModule 接口。

## 从歌曲研究迁移

参见 [原生定位两条路线](NATIVE_SELECTION_METHODS.md)：优先找到当前列表的定位与预览函数；若无法直接复用，再研究菜单进入/返回是否消费恢复项。歌曲的 5/6、6/7 状态和地址不能移植到服装菜单。

最小实验限定在已经打开的服装页、同一人物和槽位，先比较两件已知 Module 的手动高亮、预览和已装备字段。确认 ID 到当前筛选列表索引的映射，再尝试原生定位。用独立字段与截图双重验证。

高亮、预览、确认装备、保存是不同完成条件。当前曲专用与共通设置、角色槽位、人物分类不能静默切换；离开页面或更改槽位应使旧请求失效。Mod 高编号、隐藏/未解锁、发型绑定需要单独回归。

发型 KAMI 与饰品 ZUJO/FACE/NECK/BACK 可复用搜索 UI，但不能假设共享同一个原生选择函数或保存位置。
