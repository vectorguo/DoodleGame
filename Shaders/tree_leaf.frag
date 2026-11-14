#version 450
#extension GL_GOOGLE_include_directive : require

// 树叶专用片元着色器：alpha 镂空 + 双面光照。
//
// 叶片的几何是交叉的卡片：贴图的 alpha 决定哪一部分是叶子、哪一部分是空隙。
// 直接丢弃镂空处的片元（而不是混合）—— 镂空部分不该写深度，也不该参与排序，
// 材质因此保持不透明、正常写深度，叶子之间不需要按深度排序。
//
// 卡片是单层面片，从背面看时几何法线朝里：这里按 gl_FrontFacing 翻转，
// 两面才受同一盏灯照亮。对应地，这个着色器的描述文件把背面剔除关掉
// （renderState.cullMode = none）

#include "Include/DoodleInterface.glsl"
#include "Include/DoodleLighting.glsl"

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragUV;

layout(location = 0) out vec4 outColor;

// set 1：树叶贴图（颜色 + alpha 遮罩），与 lit 相同的绑定
layout(set = 1, binding = 0) uniform sampler2D albedoTexture;

void main()
{
    // FBX 的 UV 原点在左下，上传的图像首行是顶部；这里翻一次 V
    const vec2 uv = vec2(fragUV.x, 1.0 - fragUV.y);

    const vec4 albedo = texture(albedoTexture, uv);

    // 镂空：遮罩的 alpha 在叶片内部接近 1、空隙处是 0。
    // 阈值取 0.5；混合而非丢弃会让叶片之间出现排序问题，这里不做
    if (albedo.a < 0.5)
    {
        discard;
    }

    // 双面：背面看时法线朝里，翻过来才能和正面一样受光。
    // gl_FrontFacing 由光栅化给出，与描述文件里的 cullMode = none 配套
    vec3 normal = fragNormal;
    if (!gl_FrontFacing)
    {
        normal = -normal;
    }

    // 叶片偏薄透光，明暗交界比树皮还软一点：wrap = 0.35
    const vec3 baseColor = pc.baseColor.rgb * albedo.rgb;
    outColor = vec4(EvaluateLighting(normal, baseColor, 0.35), 1.0);
}
