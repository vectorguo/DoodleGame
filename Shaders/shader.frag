#version 450

// 片元着色器：对每个片元调用一次 main。
// 这里的 fragColor 已经是光栅化器在三个顶点之间插值后的值。

// 输入插槽 0，与顶点着色器里同编号的 out 配对
layout(location = 0) in vec3 fragColor;

// 片元着色器没有内置的颜色输出变量，必须自己声明。
// location 指定帧缓冲索引，这里只有一个颜色附件，所以是 0
layout(location = 0) out vec4 outColor;

void main()
{
    outColor = vec4(fragColor, 1.0);
}
