// 前向 pass 的接口块：每帧常量与逐绘制项 push constant。
//
// 这两块是 C++ 与 GLSL 之间的字节布局契约，C++ 侧定义在
// Vulkan/DoodleForwardPassData.h；这里只写一次，所有前向着色器包含同一个文件。
//
// DoodleShader 加载时会按 DoodleRenderPassLayout 反射核对：
// - 每帧常量只能是 binding 0 的一个 uniform buffer，块大小不超过 DoodleForwardPassUbo；
// - push constant 只能落在 forward pass 的 push constant 范围之内。
// 对不上直接报错，而不是画出错误的结果
//
// set/binding 与 DoodleRenderPassLayout 里的 uboBinding 对齐

layout(set = 0, binding = 0, std140) uniform ForwardPassUbo
{
    mat4 view;
    mat4 proj;
    vec4 lightDirection;   // xyz：世界空间单位向量，光线传播方向
    vec4 lightColor;       // rgb：光源颜色 × 强度
} ubo;

// 每条绘制项自己的数据：局部到世界矩阵 + 材质基色。
// 顶点阶段读 localToWorld，片元阶段读 baseColor —— 两个阶段声明的是同一段
// push constant，必须逐成员一致
layout(push_constant) uniform ForwardPassDrawItemData
{
    mat4 localToWorld;
    vec4 baseColor;
} pc;
