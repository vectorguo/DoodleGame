// 阴影 pass 的接口块（预留）：set 0 的光空间矩阵与逐绘制项 push constant。
//
// 目前没有 shadow 着色器包含它，也没有 shadow map；这里先把字节布局定下来，
// C++ 侧对应 Rendering/DoodleShadowPassData.h。真正实现阴影时：
// - 顶点着色器用 lightView / lightProj 把顶点变到光空间；
// - push constant 里的 localToWorld 与 forward 相同；
// - 需要 alpha 镂空的材质另在 set 1 声明材质纹理。
//
// set/binding 与 DoodlePassShaderInterface 里的 shadow passUboBinding 对齐

layout(set = 0, binding = 0, std140) uniform ShadowPassUbo
{
    mat4 lightView;
    mat4 lightProj;
} shadowUbo;

layout(push_constant) uniform ShadowPassPushConstants
{
    mat4 localToWorld;
} shadowPc;
