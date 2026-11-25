#version 450
#extension GL_GOOGLE_include_directive : require

// 顶点着色器：对每个顶点调用一次 main。
//
// 顶点数据由 C++ 侧的 PosNormalUvVertex（Rendering/DoodleVertex.h）定义，
// 经顶点缓冲送来。location 与管线里属性描述的 location 一一对应。
// 标准网格的顶点处理收在 Include/DoodleStandardVertex.glsl 里，
// 三个网格着色器（lit / 树干 / 树叶）共用同一份。
//
// 每帧常量与 push constant 的声明在 Include/DoodleForwardPassInterface.glsl 里

#include "Include/DoodleForwardPassInterface.glsl"
#include "Include/DoodleStandardVertex.glsl"
