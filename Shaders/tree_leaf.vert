#version 450
#extension GL_GOOGLE_include_directive : require

// 树叶专用顶点着色器。
//
// 顶点处理与 lit 相同，共用的主体在 Include/DoodleStandardVertex.glsl。
// 树叶的独立之处在片元（镂空 + 双面），以及绘制状态（不剔除）；
// 以后要加风吹摆动，也在这个文件里改，不牵动其它着色器
//
// set 0 的 UBO 与 push constant 的声明在 Include/DoodleForwardPassInterface.glsl 里

#include "Include/DoodleForwardPassInterface.glsl"
#include "Include/DoodleStandardVertex.glsl"
