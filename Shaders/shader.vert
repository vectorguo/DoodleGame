#version 450

// 顶点着色器：对每个顶点调用一次 main。
// 顶点数据已搬进 C++（Runtime/Vulkan/DoodleVertex.h 的 struct Vertex），
// 由顶点缓冲送来。location 与管线里属性描述的 location 一一对应。

// 顶点属性输入。location 编号必须与 C++ 侧 VkVertexInputAttributeDescription
// 的 location 相同 —— 变量名可以随便取，编号是两边唯一的对接口径
layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec3 inColor;

// 输出插槽 0，与片元着色器里同编号的 in 配对（靠编号，不靠变量名）
layout(location = 0) out vec3 fragColor;

void main() {
    // 末位 w 设为 1.0，这样裁剪坐标转 NDC 的除法不会改变任何东西
    gl_Position = vec4(inPosition, 0.0, 1.0);

    fragColor = inColor;
}
