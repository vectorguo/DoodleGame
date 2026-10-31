#version 450

// 顶点着色器：对每个顶点调用一次 main。
// 顶点数据已搬进 C++（Runtime/Vulkan/DoodleVertex.h 的 struct Vertex），
// 由顶点缓冲送来。location 与管线里属性描述的 location 一一对应。

// 顶点属性输入。location 编号必须与 C++ 侧 VkVertexInputAttributeDescription
// 的 location 相同 —— 变量名可以随便取，编号是两边唯一的对接口径
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;

// 顶点格式里还有第一套 UV（location = 2 in vec2）。本阶段不用它，
// 片元着色器也还没读它 —— 声明一个没有读取者的输入只会让编译器把它丢掉，
// 等贴图接进来时再加。管线里的属性描述现在就该带上它，那时不用再改顶点布局

// 每条绘制项自己的数据：局部到世界矩阵 + 材质基色，走 push constant。
// 它们不在 UBO 里：UBO 一帧一份、所有绘制项共用，装不下逐项的数据。
// 范围（stage = 顶点 + 片元、offset = 0、size = 80）由 C++ 侧管线布局声明，
// 数据块布局见 DoodlePushConstantObject，两边必须一致。
// 本阶段只读 localToWorld；baseColor 由片元阶段读，但 block 必须逐成员一致 ——
// 两个阶段声明的是同一段 push constant，不是两个独立的数据块
layout(push_constant) uniform PushConstantObject
{
    mat4 localToWorld;
    vec4 baseColor;
} pc;

// 整帧共享的数据：相机矩阵 + 主光源。
// set/binding 必须与 DoodleGraphicsPipeline 里的 VkDescriptorSetLayoutBinding
// 以及写入时的 dstBinding 对齐。光源字段本阶段不读，但两个着色器里的 block
// 必须逐成员一致 —— 这是同一个资源，不是两份数据。
// std140 是 uniform block 的排布规则：C++ 侧的 DoodleUniformBufferObject
// 用 alignas(16) 和 static_assert 保证成员偏移与这里一致
layout(set = 0, binding = 0, std140) uniform UniformBufferObject
{
    mat4 view;
    mat4 proj;
    vec4 lightDirection;   // xyz：世界空间单位向量，光线传播方向
    vec4 lightColor;       // rgb：光源颜色 × 强度
} ubo;

// 输出插槽 0，与片元着色器里同编号的 in 配对（靠编号，不靠变量名）。
// 法线经过插值后不再是单位长度，片元里会先归一化再点乘
layout(location = 0) out vec3 fragNormal;

void main() {
    // 位置补成齐次坐标，再依次乘 localToWorld / view / proj。
    // 顺序不能反：矩阵从右往左作用，顶点先进入模型空间，再到观察空间，最后到裁剪空间
    gl_Position = ubo.proj * ubo.view * pc.localToWorld * vec4(inPosition, 1.0);

    // 法线不能直接乘 localToWorld：平移会把方向当位置搬走，非均匀缩放会让法线
    // 不再垂直于原表面。逆转置的 3×3 部分才保持「垂直」这条性质 ——
    // 和 DoodleFbxImporter 处理节点变换时用的是同一条规则。
    // 插值发生在光栅化阶段，所以这里只负责变到世界空间，不归一化
    mat3 normalMatrix = transpose(inverse(mat3(pc.localToWorld)));
    fragNormal = normalMatrix * inNormal;
}
