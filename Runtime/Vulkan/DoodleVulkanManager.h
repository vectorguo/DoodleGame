//
// Created by 郭智 on 2026/9/5.
//

#pragma once

#include <array>
#include <vector>

#include "DoodleCommandBuffer.h"
#include "DoodleDepthImages.h"
#include "DoodleGraphicsBuffer.h"
#include "DoodleGraphicsPipeline.h"
#include "DoodleSwapChain.h"
#include "DoodleSyncObjects.h"
#include "DoodleVertex.h"
#include "DoodleVulkanDevice.h"
#include "../Platform/DoodleWindow.h"

namespace Doodle
{
    /**
     * Vulkan子系统门面：按依赖顺序持有各层，对外提供统一的Vulkan入口
     */
    class DoodleVulkanManager
    {
    public:
        /**
         * 初始化Vulkan
         */
        void Initialize(DoodleWindow& window);

        /**
         * 销毁Vulkan
         */
        void Destroy();

        /**
         * 表面没了：拆掉表面，以及所有依附于它的层
         *
         * Android 上切后台、锁屏、旋转都会走这条路 —— ANativeWindow 消失，
         * 基于它的 VkSurfaceKHR 随之失效，交换链与按图像分配的同步对象全部作废。
         * 桌面不会调到（窗口与程序同寿）
         *
         * 保留的是与表面无关的那几层：Instance、物理/逻辑设备、队列、
         * 图形管线、命令池、uniform 缓冲与描述符资源。重建它们代价太大，
         * 而且它们的寿命本来就不挂在窗口上
         *
         * @note 期间不得调用 DrawFrame
         */
        void Suspend();

        /**
         * 表面回来了：建出新表面，再把被 Suspend 拆掉的那几层重建起来
         *
         * 重建那一半交给 RecreateSwapChain —— 与「交换链过时」是同一条路，
         * 两边只有表面本身不同：这边要新建，那边原样沿用
         *
         * @param window 提供新的表面
         * @note 与 Suspend 必须成对。可反复调用（每次切前后台一轮）
         */
        void Resume(DoodleWindow& window);

        /**
         * 渲染并呈现一帧
         *
         * 主循环每轮调用一次。六步固定：等本槽位上一轮结束、取一张交换链图像、
         * 复位栅栏并写入本帧 UBO、录制命令缓冲、提交、呈现。顺序不能调换，
         * 每一步都在为下一步准备前提条件
         *
         * 交换链与窗口不再匹配时（缩放窗口等），本函数内部完成重建：
         * acquire 返回 out-of-date 就立刻重建并放弃本帧；呈现返回 out-of-date /
         * suboptimal，或收到过 resize 通知，则在呈现之后重建。
         * 两处判断各有一个固定位置，都不能挪 —— 理由见实现里的注释
         *
         * 同一时刻允许 MAX_FRAMES_IN_FLIGHT 帧在途：host 录第 k 帧时，
         * GPU 可以还在跑第 k-1 帧，两者用的是各自槽位的资源，不会互相踩踏
         *
         * 放在门面：它把本门面自己持有的几层按顺序驱动一遍，连同它们共有的
         * 重建时机 —— 那正是门面的分内事
         *
         * @note 里面的 Vulkan 调用大多是异步的，返回不代表 GPU 已完成
         */
        void DrawFrame();

        /**
         * 通知窗口尺寸发生了变化，下一帧需要重建交换链
         *
         * 由上层在 GLFW 的 resize 回调里调用。本层不自己监听窗口事件：
         * 回调可能在任意时刻触发，在里面做重建会打乱帧的时序。
         * 这里只记一个标志，把处理留给帧循环，顺带把连续的 resize 事件合并成一次重建
         */
        void NotifyWindowResized()
        {
            m_windowResized = true;
        }

        /**
         * 获取Vulkan设备
         */
        [[nodiscard]] DoodleVulkanDevice& GetDevice()
        {
            return m_device;
        }

        /**
         * 获取Vulkan设备
         */
        [[nodiscard]] const DoodleVulkanDevice& GetDevice() const
        {
            return m_device;
        }

        /**
         * 获取交换链
         */
        [[nodiscard]] DoodleSwapChain& GetSwapChain()
        {
            return m_swapChain;
        }

        /**
         * 获取交换链
         */
        [[nodiscard]] const DoodleSwapChain& GetSwapChain() const
        {
            return m_swapChain;
        }

        /**
         * 获取图形管线
         */
        [[nodiscard]] DoodleGraphicsPipeline& GetGraphicsPipeline()
        {
            return m_graphicsPipeline;
        }

        /**
         * 获取图形管线
         */
        [[nodiscard]] const DoodleGraphicsPipeline& GetGraphicsPipeline() const
        {
            return m_graphicsPipeline;
        }

        /**
         * 获取命令缓冲层
         */
        [[nodiscard]] DoodleCommandBuffer& GetCommandBuffer()
        {
            return m_commandBuffer;
        }

        /**
         * 获取命令缓冲层
         */
        [[nodiscard]] const DoodleCommandBuffer& GetCommandBuffer() const
        {
            return m_commandBuffer;
        }

        /**
         * 获取同步对象层
         */
        [[nodiscard]] DoodleSyncObjects& GetSyncObjects()
        {
            return m_syncObjects;
        }

        /**
         * 获取同步对象层
         */
        [[nodiscard]] const DoodleSyncObjects& GetSyncObjects() const
        {
            return m_syncObjects;
        }

    private:
        /**
         * 重建交换链，以及所有随它一起失效的层
         *
         * 顺序：交换链 → 深度图 → 同步对象，与 Initialize 同序、与 Destroy 反序。
         * 图形管线不重建 —— 它按值收下了颜色与深度格式，不持有这两个对象的任何句柄。
         * 只有格式真的变了才需要重建，那是极罕见的情况（把窗口拖到另一块
         * 不同色域的显示器上），代价却是把管线整个重编译一遍。理由详见实现
         *
         * 末尾统一清 m_windowResized：重建完成即代表积压的 resize 通知已兑现。
         * 不放在各调用点，是因为 acquire 提前返回那条路也要求这一刻就兑现它 ——
         * 漏一处就会为同一个尺寸白重建一遍；而会泵事件的 WaitUntilDrawable
         * 正在本函数开头，清零只能排在它后面
         */
        void RecreateSwapChain();

        /**
         * 把一份数据送上 GPU 能读的设备本地缓冲
         *
         * 中转流程五步：建设备本地缓冲（顺带加上 TRANSFER_DST —— 它要当拷贝的目标）→
         * 建一块 host 可见的临时中转（TRANSFER_SRC）→ 把数据写进中转 →
         * 用拷贝命令搬过去 → 销毁中转
         *
         * 为什么不直接往设备本地的缓冲里写：那块内存取的是 DEVICE_LOCAL，
         * CPU 根本映射不到它。中转存在的全部理由就是这个 —— 它是一块 CPU 和 GPU
         * 都能碰的内存，负责把数据摆到 GPU 够得着的地方，再由 GPU 自己搬一次。
         * 那一次搬运的代价换来的是正式缓冲从此走显存，不像 host 可见内存那样
         * 每次读取都过总线
         *
         * 中转在这里是局部量，不是成员：它的寿命只覆盖本函数中间几行，
         * 做成成员会凭空引入「它此刻是否有效」这个本来不存在的状态问题
         *
         * @param buffer 目标缓冲。必须是空的（尚未 Initialize）
         * @param usage 这份数据的用途，取 VERTEX_BUFFER / INDEX_BUFFER 之类。
         *        TRANSFER_DST 由本函数加上，调用方不必自己带
         * @param pData 源数据
         * @param size 数据字节数
         *
         * @note 内部会提交一次拷贝并等待完成。所以只能在初始化期调用，
         * 不得与在途的帧并发 —— 见 DoodleGraphicsBuffer::CopyTo
         */
        void CreateDeviceLocalBuffer(DoodleGraphicsBuffer& buffer, VkBufferUsageFlags usage,
                                     const void* pData, VkDeviceSize size);

        /**
         * 把 m_vertices 送上一个设备本地的顶点缓冲
         *
         * 与上一轮的差别只在内存那一头：那时取 HOST_VISIBLE | HOST_COHERENT 直接映射写入，
         * 是刻意写笨的过渡形态；现在换成设备本地 + 中转。顶点数据本身一个字节都没改
         */
        void CreateVertexBuffer();

        /**
         * 销毁顶点缓冲
         */
        void DestroyVertexBuffer();

        /**
         * 把 m_indices 送上一个设备本地的索引缓冲
         *
         * 建立过程与顶点缓冲逐字相同（都走 CreateDeviceLocalBuffer），只有 usage
         * 与数据来源两处不同 —— 所以两个函数各自只剩一行
         */
        void CreateIndexBuffer();

        /**
         * 销毁索引缓冲
         */
        void DestroyIndexBuffer();

        /**
         * 为每个在途帧建一块 uniform 缓冲并持久映射
         *
         * UBO 与顶点/索引缓冲相反：它每帧都要更新，所以走 HOST_VISIBLE |
         * HOST_COHERENT，不走设备本地 + 中转。映射只做一次，之后每帧直接 memcpy
         */
        void CreateUniformBuffers();

        /**
         * 解除映射并销毁所有 uniform 缓冲
         *
         * 调用前必须先销毁描述符池：集合指向这些缓冲，池先走，集合才失效
         */
        void DestroyUniformBuffers();

        /**
         * 把本帧的 MVP 矩阵写进第 frameIndex 块 uniform 缓冲
         *
         * 调用位置在等完本槽位栅栏之后、录制命令之前：此时 GPU 不会再读写
         * 这块缓冲的上一轮内容，而本帧命令还没开始引用它
         *
         * @param frameIndex 在途帧索引，与描述符集、命令缓冲使用同一个索引
         */
        void UpdateUniformBuffer(uint32_t frameIndex);

        /**
         * 创建描述符池
         *
         * 容量按「每在途帧一个 uniform buffer 描述符、一个集合」报；
         * 池本身不绑定队列族，也不是管线对象
         */
        void CreateDescriptorPool();

        /**
         * 销毁描述符池
         *
         * 池里的描述符集随池一起失效，不需要逐个释放；必须先于 UBO 与图形管线销毁
         */
        void DestroyDescriptorPool();

        /**
         * 从池里分配每帧一个描述符集，并把各帧 UBO 写到 binding 0
         *
         * 前置条件：池、UBO 和图形管线都已初始化。集合会一直持有这些缓冲句柄，
         * 所以交换链重建时不需要重建集合
         */
        void CreateDescriptorSets();

    private:
        /**
         * 窗口，在 Initialize 时绑定
         *
         * 只借不放：窗口的创建与销毁都归驱动层（DoodleApplication 及其平台子类，
         * macOS 见 DoodleMacApplication，Android 见 DoodleAndroidApplication）。
         * 本层需要它，是因为重建交换链时要靠它
         * 取窗口的最新像素尺寸，Suspend 之后还要靠它把表面重新建起来
         */
        DoodleWindow* m_pWindow = nullptr;

        /**
         * Vulkan设备层：Instance / Surface / 物理设备 / 逻辑设备 / 队列
         */
        DoodleVulkanDevice m_device;

        /**
         * 交换链层：窗口的呈现目标
         */
        DoodleSwapChain m_swapChain;

        /**
         * 深度图组：每帧一张，随交换链重建
         *
         * 排在交换链之后（要它的尺寸）、图形管线之前（管线要它的格式）。
         * 它与交换链同锚不同寿：尺寸跟着窗口变，但它不是呈现目标，
         * 不与呈现引擎打交道
         */
        DoodleDepthImages m_depthImages;

        /**
         * 图形管线层：只读引用设备层、交换链层（取颜色附件格式）与深度图组
         * （取深度格式），须排在交换链与深度图组之后
         */
        DoodleGraphicsPipeline m_graphicsPipeline;

        /**
         * 命令缓冲层：录制时引用上述各层，须排在图形管线之后
         */
        DoodleCommandBuffer m_commandBuffer;

        /**
         * 顶点数据：一个以原点为中心、边长为 1 的正方体
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
         * 每个面四个顶点排成一个圈，两个三角形怎么切是 m_indices 的事
         */
        const std::vector<Vertex> m_vertices = {
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
        };

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
         * 顶点数越过 65535 时必须换成 uint32_t，那时 DoodleCommandBuffer::Record 里
         * 写死的 VK_INDEX_TYPE_UINT16 要跟着一起改
         */
        const std::vector<uint16_t> m_indices = {
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
        };

        /**
         * 顶点缓冲
         *
         * 锚在程序生命周期上，与窗口尺寸无关，所以不进 RecreateSwapChain 那条链 ——
         * 拖窗口不该让顶点数据重新上传一遍
         */
        DoodleGraphicsBuffer m_vertexBuffer;

        /**
         * 索引缓冲
         *
         * 寿命与 m_vertexBuffer 同：建一次，用到底
         */
        DoodleGraphicsBuffer m_indexBuffer;

        /**
         * 每帧一块 uniform 缓冲，持久映射
         *
         * 它们不随交换链重建：窗口缩放只改变投影矩阵的内容，不改变缓冲对象。
         * 数量必须与 MAX_FRAMES_IN_FLIGHT 一致，否则当前帧的 UBO 会和
         * 上一轮仍在 GPU 上读取的数据重叠
         */
        std::array<DoodleGraphicsBuffer, MAX_FRAMES_IN_FLIGHT> m_uniformBuffers;

        /**
         * 描述符池：为每帧的描述符集提供分配空间
         *
         * 归门面所有，而不是图形管线层：它服务的是每帧 UBO 的接线，
         * 与管线配置无关。销毁时必须排在 UBO 和图形管线之前
         */
        VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;

        /**
         * 每帧一个描述符集，各自把该帧的 UBO 接到 binding 0
         *
         * 集合的句柄随池释放，不需要单独销毁；与 m_uniformBuffers 使用同一个
         * frameIndex 取用
         */
        std::array<VkDescriptorSet, MAX_FRAMES_IN_FLIGHT> m_descriptorSets{};

        /**
         * 同步对象层：帧信号量与栅栏
         *
         * renderFinished 按交换链图像数量分配，所以它依赖交换链层，
         * 且随交换链一起重建，须排在交换链之后
         */
        DoodleSyncObjects m_syncObjects;

        /**
         * 当前在途帧索引，取值 0 .. MAX_FRAMES_IN_FLIGHT-1，DrawFrame 开头取用后立即推进一格
         *
         * 命令缓冲、uniform 缓冲、描述符集、imageAvailable 信号量、帧栅栏都按它轮转取用。
         * renderFinished 不按它取 —— 那是按交换链图像索引的，两个维度互不相干
         */
        uint32_t m_currentFrame = 0;

        /**
         * 窗口尺寸变化标志
         *
         * 由 NotifyWindowResized 置位，DrawFrame 在呈现之后消费并清零。
         * 不在回调里当场重建，是为了把连续多次 resize 合并到帧边界只处理一次
         */
        bool m_windowResized = false;
    };
}
