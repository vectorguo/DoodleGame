//
// Created by 郭智 on 2026/9/5.
//

#pragma once

#include <cstdint>
#include <vector>

#include "DoodleCommandBuffer.h"
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
         * 图形管线、命令池。重建它们代价太大且没有必要
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
         * 复位栅栏、录制命令缓冲、提交、呈现。顺序不能调换，
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
         * 顺序：交换链 → 同步对象，与 Initialize 同序、与 Destroy 反序。
         * 图形管线不重建 —— 它按值收下了颜色附件格式，不持有交换链的任何句柄。
         * 只有交换链格式真的变了才需要重建，那是极罕见的情况（把窗口拖到另一块
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
         * 图形管线层：只读引用设备层与交换链层（取颜色附件格式），须排在交换链之后
         */
        DoodleGraphicsPipeline m_graphicsPipeline;

        /**
         * 命令缓冲层：录制时引用上述各层，须排在图形管线之后
         */
        DoodleCommandBuffer m_commandBuffer;

        /**
         * 顶点数据：一个矩形的四个角，位置 + 颜色
         *
         * 顺序是 左上 → 右上 → 右下 → 左下，配着图形管线里的 frontFace = CLOCKWISE
         * 与 cullMode = BACK 才画得出东西 —— 两个三角形都是顺时针，重排会让它们
         * 成片消失，而且不报任何错。改动这个顺序之前，
         * 先回去看 DoodleGraphicsPipeline 里光栅化那两行的注释
         *
         * 注意 Vulkan 的裁剪空间里 Y 轴朝下，所以「左上」的 y 是负的
         *
         * 这四个顶点本身排成一个圈，并没有说三角形怎么取 —— 哪三个一组是
         * m_indices 的事。这里只是把矩形需要的顶点各摆一份
         */
        const std::vector<Vertex> m_vertices = {
            {{-0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}},
            {{0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}},
            {{0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}},
            {{-0.5f, 0.5f}, {1.0f, 1.0f, 1.0f}}
        };

        /**
         * 索引数据：两个三角形，六个索引，指向上面四个顶点
         *
         * 两两一对读：{0,1,2} 是 左上→右上→右下，{2,3,0} 是 右下→左下→左上。
         * 两个三角形共用对角线上的 2 与 0，所以 4 个顶点就够 ——
         * 老老实实列两个三角形要 6 份顶点数据，这里省掉 2 份
         *
         * 用 uint16_t 而不是 uint32_t：索引的位数只由**唯一顶点数**决定，
         * 与画几个三角形无关。本矩形 4 个唯一顶点，索引最大是 3，16 位绰绰有余。
         * 白拿两个好处：数据小一半，且一次缓存行能装下更多索引 ——
         * 索引是 GPU 顺序流式读取的数据，装得密命中率就高
         *
         * 顶点数越过 65535 时必须换成 uint32_t，那时 DoodleCommandBuffer::Record 里
         * 写死的 VK_INDEX_TYPE_UINT16 要跟着一起改
         */
        const std::vector<uint16_t> m_indices = {
            0, 1, 2, 2, 3, 0
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
         * 同步对象层：帧信号量与栅栏
         *
         * renderFinished 按交换链图像数量分配，所以它依赖交换链层，
         * 且随交换链一起重建，须排在交换链之后
         */
        DoodleSyncObjects m_syncObjects;

        /**
         * 当前在途帧索引，取值 0 .. MAX_FRAMES_IN_FLIGHT-1，DrawFrame 开头取用后立即推进一格
         *
         * 命令缓冲、imageAvailable 信号量、帧栅栏都按它轮转取用。
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
