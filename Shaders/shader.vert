#version 450

// 顶点着色器：对每个顶点调用一次 main。
// 顶点数据已搬进 C++（Runtime/Vulkan/DoodleVertex.h 的 struct Vertex），
// 由顶点缓冲送来。location 与管线里属性描述的 location 一一对应。

// 顶点属性输入。location 编号必须与 C++ 侧 VkVertexInputAttributeDescription
// 的 location 相同 —— 变量名可以随便取，编号是两边唯一的对接口径
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;

// 每帧的 MVP 矩阵。set/binding 必须与 DoodleGraphicsPipeline 里的
// VkDescriptorSetLayoutBinding 以及写入时的 dstBinding 对齐。
// std140 是 uniform block 的排布规则：C++ 侧的 UniformBufferObject
// 用 alignas(16) 和 static_assert 保证成员偏移与这里一致
layout(set = 0, binding = 0, std140) uniform UniformBufferObject
{
    mat4 model;
    mat4 view;
    mat4 proj;
} ubo;

// 输出插槽 0，与片元着色器里同编号的 in 配对（靠编号，不靠变量名）
layout(location = 0) out vec3 fragColor;

void main() {
    // 把位置补成齐次坐标，再依次乘 model / view / proj。
    // 顺序不能反：矩阵从右往左作用，顶点先进入模型空间，再到观察空间，最后到裁剪空间
    gl_Position = ubo.proj * ubo.view * ubo.model * vec4(inPosition, 1.0);

    fragColor = inColor;
}
