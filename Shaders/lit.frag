#version 450
#extension GL_GOOGLE_include_directive : require

// 片元着色器：对每个片元调用一次 main。
//
// 通用不透明网格：采样材质纹理、乘材质基色，再做方向光 + 环境项。
// 所有网格的兜底着色器 —— 材质没写 shader 时用它。

#include "Include/DoodleForwardPassInterface.glsl"
#include "Include/DoodleLighting.glsl"

// 输入插槽 0：顶点着色器送来的世界空间法线。已经过光栅化插值，
// 长度一般不再是 1，使用前必须归一化
layout(location = 0) in vec3 fragNormal;

// 输入插槽 1：顶点的第一套 UV，片元阶段按需翻转 V 后采样
layout(location = 1) in vec2 fragUV;

// 片元着色器没有内置的颜色输出变量，必须自己声明。
// location 指定帧缓冲索引，这里只有一个颜色附件，所以是 0
layout(location = 0) out vec4 outColor;

// set 1：材质贴图。set 1 里声明几张 sampler2D，这个着色器就有几个贴图槽；
// 材质按变量名配贴图（"textures": { "albedoTexture": ... }）。材质没配时绘制侧
// 绑定 1×1 白图，所以这里不需要「有没有贴图」的分支，采样永远只有一条路。
// 规则见 DoodleMaterialShaderInterface.h（DoodleShader 加载时反射核对）；
// 采样器随贴图一起写入（COMBINED_IMAGE_SAMPLER），不在着色器里另声明
layout(set = 1, binding = 0) uniform sampler2D albedoTexture;

void main()
{
    // FBX 的 UV 原点在左下，上传的图像首行是顶部；这里翻一次 V，
    // 让 v = 0 对应图像底部。将来接 glTF（V 向下）时，翻转要按资产约定区分
    const vec2 uv = vec2(fragUV.x, 1.0 - fragUV.y);

    // 材质基色由绘制项经 push constant 送来，CPU 侧已经转成线性。
    // 纹理按 SRGB 格式上传，采样结果同样是线性；两者直接相乘，
    // 不在不同色彩空间之间做混合
    const vec4 albedo = texture(albedoTexture, uv);
    const vec3 baseColor = pc.baseColor.rgb * albedo.rgb;

    // wrap = 0：标准 Lambert
    outColor = vec4(EvaluateLighting(fragNormal, baseColor, 0.0), 1.0);
}
