# 01 所有 pass 共用一份绘制列表，set 1 按下标对齐

类别：结构 ｜ 优先级：高 ｜ 建议时机：做阴影之前 ｜ 关联：02、04、11

## 现状

- 绘制项由应用层收集：`DoodleApplication::CollectDrawItems`（`Engine/Runtime/Platform/DoodleApplication.cpp`）遍历所有场景的所有对象，每个对象调一次 `GetComponent<DoodleMeshRenderer>()`（线性扫描 + `dynamic_cast`），再按子网格展开成 `DoodleDrawItem`。整帧只有这一份列表，顺序就是遍历顺序。
- 材质的 set 1 由 `DoodleDescriptorManager::ResolveMaterialDescriptorSets` 每帧解析一次，结果是一个与 `drawItems` 等长、按下标一一对应的数组，放在 `DoodleRenderPassContext::materialDescriptorSets` 里给所有 pass 共用。描述符管理器内部其实已经按材质指针缓存了集合（`m_materialDescriptorSets`），这个数组只是按绘制项下标摊开的一份投影。
- 相机、光源各自有注册表（`DoodleCamera::s_cameras`、`DoodleLight::s_lights`），MeshRenderer 没有。

## 问题

- 阴影 pass 要画的是另一份列表：投射物（以后按 castShadows 过滤、按光源视锥裁剪）。它的下标和 `drawItems` 对不上，现在的 `materialDescriptorSets` 没法复用。以后透明物体单独排序、做视锥裁剪，也是同一个问题。
- 收集逻辑（子网格展开、按槽取材质）是渲染侧的事，却放在平台无关的应用层；渲染侧想按 pass 决定「画什么」，就得去改应用层。
- 每帧对每个对象做一次组件线性查找，对象多了是一笔和「它有没有渲染器」无关的固定开销。

## 建议

- set 1 改成按材质取（例如 `GetMaterialDescriptorSet(const DoodleMaterial&)`，或者在 Prepare 阶段把句柄写进每个 pass 自己的列表，见 04），不再要求和某一份列表的下标对齐。
- 收集挪到渲染侧（`DoodleRenderManager` / `DoodleRenderPipeline` 的提取步骤）：先得到本帧的可见对象集合，各 pass 再从中过滤出自己的列表（前向：全部不透明物体；阴影：投射物）。
- MeshRenderer 像相机 / 光源一样，在 OnAwake / OnDestroy 里登记到注册表，收集时直接遍历注册表；顺带加一个 castShadows 开关给阴影 pass 过滤用。

## 解决标志

阴影 pass 能用自己的投射物列表拿到每个材质的 set 1，不依赖 `drawItems` 的下标；应用层不再收集绘制项。
