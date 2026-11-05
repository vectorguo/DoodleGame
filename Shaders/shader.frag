#version 450

// 片元着色器：对每个片元调用一次 main。

// 输入插槽 0：顶点着色器送来的世界空间法线。已经过光栅化插值，
// 长度一般不再是 1，使用前必须归一化
layout(location = 0) in vec3 fragNormal;

// 输入插槽 1：顶点的第一套 UV，片元阶段按需翻转 V 后采样
layout(location = 1) in vec2 fragUV;

// 片元着色器没有内置的颜色输出变量，必须自己声明。
// location 指定帧缓冲索引，这里只有一个颜色附件，所以是 0
layout(location = 0) out vec4 outColor;

// 与顶点着色器里逐成员一致。片元阶段只读两个光源字段，
// 但 block 是个整体，声明必须完全相同
layout(set = 0, binding = 0, std140) uniform UniformBufferObject
{
    mat4 view;
    mat4 proj;
    vec4 lightDirection;   // xyz：世界空间单位向量，光线传播方向
    vec4 lightColor;       // rgb：光源颜色 × 强度
} ubo;

// set 1：材质纹理。材质没有配置纹理时，绘制侧绑定 1×1 白图，
// 所以这里不需要「有没有纹理」的分支，采样永远只有一条路。
// set/binding 必须与 DoodleGraphicsPipeline 的 set 1 布局一致；
// 采样器随纹理一起写入（COMBINED_IMAGE_SAMPLER），不在着色器里另声明
layout(set = 1, binding = 0) uniform sampler2D albedoTexture;

// 与顶点着色器里逐成员一致。本阶段只读 baseColor，
// 但 block 是个整体，声明必须完全相同
layout(push_constant) uniform PushConstantObject
{
    mat4 localToWorld;
    vec4 baseColor;
} pc;

void main()
{
    // 材质基色由绘制项经 push constant 送来，CPU 侧已经转成线性。
    // 纹理按 SRGB 格式上传，采样结果同样是线性；两者直接相乘，
    // 不在不同色彩空间之间做混合
    //
    // FBX 的 UV 原点在左下，上传的图像首行是顶部；这里翻一次 V，
    // 让 v = 0 对应图像底部。将来接 glTF（V 向下）时，翻转要按资产约定区分
    const vec2 uv = vec2(fragUV.x, 1.0 - fragUV.y);
    const vec4 albedo = texture(albedoTexture, uv);
    const vec3 baseColor = pc.baseColor.rgb * albedo.rgb;

    // 环境项：背光面也不至于全黑。它由常量给出，不受光源颜色影响 ——
    // 场景里没有光源时（lightColor 为 0），画面就只剩这一项
    const float ambient = 0.22;

    vec3 normal = normalize(fragNormal);

    // lightDirection 是光线传播方向（从光源指向场景），
    // 朝向光源的那一侧是它的反方向；点乘小于 0 的部分是背光面，
    // 由环境项兜底，所以这里夹到 0
    float diffuse = max(dot(normal, -ubo.lightDirection.xyz), 0.0);

    // 环境项 + 漫反射，漫反射的上限吃掉 ambient：白光照满时正好回到 baseColor，
    // 不会因为两项相加而超过基色
    vec3 color = baseColor * (ambient + (1.0 - ambient) * diffuse * ubo.lightColor.rgb);
    outColor = vec4(color, 1.0);
}
