#version 450

// 顶点着色器：对每个顶点调用一次 main。
// 本阶段刻意把顶点数据写死在着色器里，等「顶点缓冲」那一章再换掉。

// 输出插槽 0，与片元着色器里同编号的 in 配对（靠编号，不靠变量名）
layout(location = 0) out vec3 fragColor;

// 直接给出 NDC 坐标。Vulkan 的 Y 轴向下，所以「靠上」的顶点是负值
vec2 positions[3] = vec2[](
    vec2(0.0, -0.5),
    vec2(0.5, 0.5),
    vec2(-0.5, 0.5)
);

vec3 colors[3] = vec3[](
    vec3(1.0, 0.0, 0.0),
    vec3(0.0, 1.0, 0.0),
    vec3(0.0, 0.0, 1.0)
);

void main() {
    // gl_VertexIndex：内置输入，当前顶点编号
    // 末位 w 设为 1.0，这样裁剪坐标转 NDC 的除法不会改变任何东西
    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);

    fragColor = colors[gl_VertexIndex];
}
