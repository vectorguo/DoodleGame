#version 450
#extension GL_GOOGLE_include_directive : require

// 树干专用顶点着色器。
//
// 顶点处理与 lit 完全相同（标准顶点格式、同一套 localToWorld / 法线变换），
// 共用的主体在 Include/DoodleStandardVertex.glsl；独立成文件是为了给树皮特有的
// 顶点效果留位置（例如以后按高度做 UV 缩放、风摆只作用于树枝）
//
// set 0 的 UBO 与 push constant 的声明在 Include/DoodleInterface.glsl 里

#include "Include/DoodleInterface.glsl"
#include "Include/DoodleStandardVertex.glsl"
