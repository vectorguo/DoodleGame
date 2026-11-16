#version 450
#extension GL_GOOGLE_include_directive : require

// 树干专用片元着色器。
//
// 与 lit 的区别只有一处：光照用 wrap = 0.25 的软交界（见 EvaluateLighting）。
// 树皮是有机材质、表面起伏多，标准 Lambert 的明暗交界线在低模树干上会是一道
// 生硬的折线；把交界线往背光面推一点更接近树皮的观感。
//
// 树皮不透明、单面（背面剔除），所以这里既不做 alpha 镂空，也不翻背面法线 ——
// 那是树叶着色器的事

#include "Include/DoodleForwardPassInterface.glsl"
#include "Include/DoodleLighting.glsl"

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragUV;

layout(location = 0) out vec4 outColor;

// set 1：材质纹理（树皮贴图），与 lit 相同的绑定
layout(set = 1, binding = 0) uniform sampler2D albedoTexture;

void main()
{
    // FBX 的 UV 原点在左下，上传的图像首行是顶部；这里翻一次 V
    const vec2 uv = vec2(fragUV.x, 1.0 - fragUV.y);

    const vec4 albedo = texture(albedoTexture, uv);
    const vec3 baseColor = pc.baseColor.rgb * albedo.rgb;

    outColor = vec4(EvaluateLighting(fragNormal, baseColor, 0.25), 1.0);
}
