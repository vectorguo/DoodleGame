//
//  DoodleMeshLoader.cpp
//  Doodle
//
//  Created by 郭智 on 2026/10/25.
//

#include "DoodleMeshLoader.h"

#include <array>
#include <stdexcept>
#include <string>
#include <string_view>

#include "Vulkan/DoodleVulkanDevice.h"

namespace
{
    /**
     * 内建正方体的名字，场景文件里引用的就是它
     */
    constexpr std::string_view kBuiltinCubeName = "builtin:cube";

    /**
     * 内建正方体：一个以原点为中心、边长为 1 的正方体
     *
     * 六个面各用 4 个顶点，共 24 个；同一个面的四个顶点同色。
     * 不用共享的 8 个顶点，是因为现在每个面需要独立的颜色，
     * 而共享顶点只能有一份颜色，面与面之间会插值出脏色。
     * 等有了法线 / UV，这种按面拆点的做法也正好继续用
     *
     * 每个面的四个顶点都按「站在面外看，逆时针」排列。
     * 顶点着色器的投影矩阵做了 Y 翻转，屏幕空间绕序会再反一次，
     * 所以管线配的是 frontFace = COUNTER_CLOCKWISE 与 cullMode = BACK。
     * 重排任何一个面的顶点，都可能让它被当成背面剔除，而且不报错
     *
     * 相机在 (2, 2, 2) 看向原点，静止时能看到 +X、+Y、+Z 三个面
     *
     * 每个面四个顶点排成一个圈，两个三角形怎么切是 kCubeIndices 的事
     */
    const std::array<Doodle::Vertex, 24> kCubeVertices = {{
        //+X 面：橙色
        {{0.5f, -0.5f, -0.5f}, {1.00f, 0.45f, 0.10f}},
        {{0.5f, 0.5f, -0.5f}, {1.00f, 0.45f, 0.10f}},
        {{0.5f, 0.5f, 0.5f}, {1.00f, 0.45f, 0.10f}},
        {{0.5f, -0.5f, 0.5f}, {1.00f, 0.45f, 0.10f}},

        //-X 面：红色
        {{-0.5f, -0.5f, 0.5f}, {0.90f, 0.20f, 0.30f}},
        {{-0.5f, 0.5f, 0.5f}, {0.90f, 0.20f, 0.30f}},
        {{-0.5f, 0.5f, -0.5f}, {0.90f, 0.20f, 0.30f}},
        {{-0.5f, -0.5f, -0.5f}, {0.90f, 0.20f, 0.30f}},

        //+Y 面：绿色
        {{-0.5f, 0.5f, -0.5f}, {0.20f, 0.75f, 0.35f}},
        {{-0.5f, 0.5f, 0.5f}, {0.20f, 0.75f, 0.35f}},
        {{0.5f, 0.5f, 0.5f}, {0.20f, 0.75f, 0.35f}},
        {{0.5f, 0.5f, -0.5f}, {0.20f, 0.75f, 0.35f}},

        //-Y 面：青色
        {{-0.5f, -0.5f, 0.5f}, {0.15f, 0.65f, 0.75f}},
        {{-0.5f, -0.5f, -0.5f}, {0.15f, 0.65f, 0.75f}},
        {{0.5f, -0.5f, -0.5f}, {0.15f, 0.65f, 0.75f}},
        {{0.5f, -0.5f, 0.5f}, {0.15f, 0.65f, 0.75f}},

        //+Z 面：蓝色
        {{-0.5f, -0.5f, 0.5f}, {0.25f, 0.50f, 1.00f}},
        {{0.5f, -0.5f, 0.5f}, {0.25f, 0.50f, 1.00f}},
        {{0.5f, 0.5f, 0.5f}, {0.25f, 0.50f, 1.00f}},
        {{-0.5f, 0.5f, 0.5f}, {0.25f, 0.50f, 1.00f}},

        //-Z 面：黄色
        {{-0.5f, 0.5f, -0.5f}, {0.85f, 0.75f, 0.20f}},
        {{0.5f, 0.5f, -0.5f}, {0.85f, 0.75f, 0.20f}},
        {{0.5f, -0.5f, -0.5f}, {0.85f, 0.75f, 0.20f}},
        {{-0.5f, -0.5f, -0.5f}, {0.85f, 0.75f, 0.20f}}
    }};

    /**
     * 索引数据：十二个三角形，三十六个索引，指向上面二十四个顶点
     *
     * 每个面由自己那 4 个顶点组成两个三角形：0,1,2 与 0,2,3。
     * 六个面依次是 +X、-X、+Y、-Y、+Z、-Z，所以每组的基址是
     * 0、4、8、12、16、20
     *
     * 用 uint16_t 而不是 uint32_t：现在 24 个唯一顶点，索引最大是 23，
     * 16 位绰绰有余。索引位数只由唯一顶点数决定，与画几个三角形无关
     *
     * 顶点数越过 65535 时必须换成 uint32_t，那时 DoodleMesh 的接口与
     * DoodleCommandBuffer::Record 里写死的 VK_INDEX_TYPE_UINT16 要一起改
     */
    const std::array<uint16_t, 36> kCubeIndices = {{
        //+X
        0, 1, 2, 0, 2, 3,
        //-X
        4, 5, 6, 4, 6, 7,
        //+Y
        8, 9, 10, 8, 10, 11,
        //-Y
        12, 13, 14, 12, 14, 15,
        //+Z
        16, 17, 18, 16, 18, 19,
        //-Z
        20, 21, 22, 20, 22, 23
    }};
}

namespace Doodle
{
    DoodleMeshLoader::DoodleMeshLoader(const DoodleVulkanDevice& device)
        : m_pDevice(&device)
    {
        // 这里不做任何上传：内建网格也要等到第一次 Load 才建。
        // loader 注册是初始化链上最不该失败的一步，真正的资源错误
        // 留给 Create，报错时还能带上具体 key
    }

    std::unique_ptr<DoodleMesh> DoodleMeshLoader::Create(const DoodleAssetLoadContext&,
                                                              const std::string_view key)
    {
        if (key != kBuiltinCubeName)
        {
            // 网格名写错是内容错误，必须在建场景之前暴露出来。
            // 将来的文件网格在这里补「按路径导入」
            throw std::runtime_error("mesh asset not found: " + std::string(key));
        }

        // 内建网格走的是和将来外部网格同一条路：建 DoodleMesh、上传，
        // 由基类登记进缓存。没有任何「内建专用」的旁路
        auto pMesh = std::make_unique<DoodleMesh>();
        pMesh->Initialize(*m_pDevice, kCubeVertices, kCubeIndices);
        return pMesh;
    }
}
