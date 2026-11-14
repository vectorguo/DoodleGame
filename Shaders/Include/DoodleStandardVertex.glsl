// 标准顶点格式（EVertexFormat::PosNormalUv）的输入与顶点着色器主体。
//
// location 编号必须与 DoodleVertexInput::Describe 的 location 相同 ——
// 变量名可以随便取，编号是两边唯一的对接口径。
//
// 所有网格类着色器（lit / 树干 / 树叶）共用这一份：位置、法线、UV 的处理
// 对它们完全相同，只有片元怎么用这些值才不同。需要不同顶点行为时
// （例如树叶随风摆动），那个着色器不再包含本文件

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;

// 输出插槽 0：世界空间法线，与片元同编号的 in 配对（靠编号，不靠变量名）。
// 法线经过插值后不再是单位长度，片元里会先归一化
layout(location = 0) out vec3 fragNormal;

// 输出插槽 1：UV。输入编号 2 是顶点属性槽，输出编号 1 是阶段间插槽，两套编号互不相干
layout(location = 1) out vec2 fragUV;

void main()
{
    // 位置补成齐次坐标，再依次乘 localToWorld / view / proj。
    // 顺序不能反：矩阵从右往左作用，顶点先进入模型空间，再到观察空间，最后到裁剪空间
    gl_Position = ubo.proj * ubo.view * pc.localToWorld * vec4(inPosition, 1.0);

    // 法线不能直接乘 localToWorld：平移会把方向当位置搬走，非均匀缩放会让法线
    // 不再垂直于原表面。逆转置的 3×3 部分才保持「垂直」这条性质 ——
    // 和 DoodleFbxImporter 处理节点变换时用的是同一条规则。
    // 插值发生在光栅化阶段，所以这里只负责变到世界空间，不归一化
    const mat3 normalMatrix = transpose(inverse(mat3(pc.localToWorld)));
    fragNormal = normalMatrix * inNormal;

    // UV 直接透传，采样方向（V 轴）在片元着色器里统一处理
    fragUV = inUV;
}
