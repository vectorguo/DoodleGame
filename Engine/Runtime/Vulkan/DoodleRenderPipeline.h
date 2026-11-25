//
//  DoodleRenderPipeline.h
//  Doodle
//
//  Created by 郭智 on 2026/11/9.
//

#pragma once

#include <array>

#include "DoodleDepthImages.h"
#include "DoodleDescriptorManager.h"
#include "DoodleForwardPass.h"
#include "DoodleShadowPass.h"
#include "DoodleVulkanPipelineCompileCache.h"
#include "DoodlePipelineWarmupQueue.h"

namespace Doodle
{
    //只在接口签名与成员指针里出现，完整定义留给源文件
    class DoodleVulkanDevice;
    class DoodleSwapChain;
    class DoodleVulkanManager;
    class DoodleRenderManager;
    struct DoodleFrameContext;

    /**
     * 渲染管线：项目级的渲染策略
     *
     * 一个实例回答「这一帧怎么画」：持有渲染策略所需的常驻资源 —— 深度图、
     * 材质描述符管理器、驱动编译缓存 —— 以及按顺序执行的 pass。
     * 现在有一个前向 pass 和一个预留的 shadow pass；阴影渲染尚未实现，
     * shadow pass 只占编排位置。以后后处理到来时加进同一个实例，由本类决定顺序。
     *
     * **每帧常量归各自的 pass**：相机 / 光源这类数据由前向 pass 自己写进它的
     * per-pass UBO；本类不再持有 frame UBO，也不认识相机与光源。
     *
     * **与 DoodleRenderPass 的分工**：本类管「有几个 pass、按什么顺序、它们共用什么
     * 资源」；每个 pass 管自己那一段怎么录（附件、屏障、管线、绘制）。
     *
     * **与 DoodleRenderManager / DoodleVulkanManager 的分工**：本类不碰交换链获取、
     * 栅栏、提交与呈现 —— 那些是后端与表面的事。DoodleRenderManager 持有本类，
     * 按「后端取帧 → 本类录制 → 后端提交」驱动；本类只在渲染管理器给的帧索引与命令缓冲上工作。
     *
     * **生命周期**：由 DoodleRenderManager 持有，排在 DoodleVulkanManager 之后建
     * （描述符管理器的默认白图要借后端的命令层传输池）；销毁时先于后端。
     * 表面消失时只拆深度图这类跟随交换链的资源，管线与描述符保留。
     *
     * **预热队列**：待预热队列是本类的成员 —— 它的产出物就是本类持有的管线。
     * 生产者（DoodleShader::Initialize）只通过队列的单例入口投递，不需要认识本类。
     *
     * 不可复制：成员各自持有句柄与资源
     */
    class DoodleRenderPipeline
    {
        friend class DoodleRenderManager;

        /**
         * 只能由 DoodleRenderManager 创建：运行时只有一份，实例由渲染管理器
         * 独占持有。构造即登记为当前实例，析构时清空
         */
        DoodleRenderPipeline()
        {
            s_pInstance = this;
        }

    public:
        ~DoodleRenderPipeline()
        {
            if (s_pInstance == this)
            {
                s_pInstance = nullptr;
            }
        }

        DoodleRenderPipeline(const DoodleRenderPipeline&) = delete;
        DoodleRenderPipeline& operator=(const DoodleRenderPipeline&) = delete;

        /**
         * 当前那一份渲染管线
         *
         * 有效期与对象同寿（应用构造 → 应用析构）；「能用了」要到 Initialize
         * 成功之后、Destroy 之前。Initialize 之前 / Destroy 之后取到的是空壳，
         * 与 DoodleCommandManager 的口径一致。
         *
         * 只给渲染内部（pass、诊断、预热）用：内容层与玩法层需要的入口是
         * DoodleRenderManager —— 「这一帧怎么画」不该被绕开帧顺序直接驱动
         */
        [[nodiscard]] static DoodleRenderPipeline& GetInstance()
        {
            return *s_pInstance;
        }

        /**
         * 建出常驻渲染资源与全部 pass
         *
         * 顺序：深度图 → 材质描述符管理器 → 驱动编译缓存 → shadow pass → 前向 pass。
         * 深度图要交换链尺寸；描述符管理器的默认白图要借命令管理层的传输池；
         * 两个 pass 都只要设备与编译缓存 —— 各自建自己的 per-pass 常量资源。
         *
         * @param vulkanManager 已初始化的 Vulkan 后端：本类从这里取设备、交换链
         *        以及以后可能需要的其他后端服务。参数收成一个对象，后端新增服务时
         *        本签名与调用点都不动；本类不保存这个引用，只在初始化期间使用
         *
         * @note 前置条件：命令管理层已经初始化 —— 描述符管理器的默认白图上传
         *       要借它的传输池。这也是 DoodleRenderManager 把本类排在后端之后的原因
         */
        void Initialize(const DoodleVulkanManager& vulkanManager);

        /**
         * 拆掉本类持有的全部资源
         *
         * 顺序与 Initialize 相反：前向 pass → shadow pass → 驱动编译缓存 →
         * 描述符管理器 → 深度图。使用者先、被使用者后：pass 录制时绑的是描述符
         * 管理器里的材质集合，所以 pass 排在它之前。
         *
         * 开头先清空待预热队列：里面是着色器的非拥有指针，资产可能已经先于本类
         * 销毁；清空只丢指针、不解引用，所以放在资源拆除之前即可。
         *
         * 可以重复调用。前置条件：GPU 已空闲 —— 在途命令可能还引用着管线与深度图
         */
        void Destroy();

        /**
         * 把本帧录进给定的命令缓冲
         *
         * 从帧上下文读取帧索引、图像索引、命令缓冲与应用输入：解析本帧的材质
         * 描述符集，然后交给每个 pass 录制。每个 pass 自己负责写本帧常量 ——
         * 写入位置必须在本槽位栅栏之后、该 pass 的命令录制之前。
         *
         * @param frameContext BeginFrame 填好状态字段的帧上下文
         *
         * @note 前置条件：context.status == Ready
         */
        void Render(const DoodleFrameContext& frameContext);

        /**
         * 排空待预热队列，让每个 pass 把新着色器的管线建出来
         *
         * 由 DoodleRenderManager::Render 在取帧之前每帧调用一次。输入不是绘制项：
         * 每个着色器在每个 pass 里只有一条管线，跟本帧画了多少东西无关。
         * 队列为空时直接返回，稳态开销是常数。
         *
         * 按 pass 顺序逐个分发 —— 每个 pass 自己决定要哪些管线，本类只负责
         * 排空与分发，不碰附件、描述符集与命令缓冲。
         *
         * @throw 管线建不出来时向上抛，由渲染管理器在取帧之前抛给应用
         */
        void WarmupPipelines();

        /**
         * 表面消失前调用：拆掉跟随交换链的渲染资源
         *
         * 现在只有深度图 —— 它按交换链尺寸分配。管线、per-pass 常量与描述符资源
         * 都与表面无关，保留下来；重建它们的代价远大于留着。
         *
         * @note 前置条件：GPU 已空闲；之后不得再调用 Render，直到
         *       OnSwapChainRecreated
         */
        void OnSurfaceLost();

        /**
         * 交换链重建后调用：按新尺寸重建深度图，并让 pass 按新附件格式重建管线
         *
         * 会先销毁旧深度图再按新尺寸建（Destroy 幂等，OnSurfaceLost 之后调用也安全）。
         * 尺寸变化不影响管线（视口与剪裁是动态状态）；只有颜色 / 深度格式或
         * 采样数真的变了，对应 pass 的管线才会原地重建。每个 pass 都通知一遍，
         * 变没变由 pass 自己按附件判断。
         *
         * @note 前置条件：新的交换链已经初始化
         */
        void OnSwapChainRecreated();

    private:
        /**
         * 按渲染顺序列出本类持有的 pass
         *
         * Render / WarmupPipelines / OnSwapChainRecreated 正序遍历，Destroy 逆序遍历。
         * 集中在一处，以后加 pass 时不会漏掉某个调用点
         */
        [[nodiscard]] std::array<DoodleRenderPass*, 2> GetRenderPasses();

    private:
        /**
         * 当前实例，构造时登记、析构时清空
         */
        static DoodleRenderPipeline* s_pInstance;

        /**
         * Vulkan设备层，在 Initialize 时绑定
         */
        const DoodleVulkanDevice* m_pDevice = nullptr;

        /**
         * 交换链，在 Initialize 时绑定；取尺寸、颜色格式与预旋转用
         *
         * 只借不放，寿命必须覆盖本类
         */
        const DoodleSwapChain* m_pSwapChain = nullptr;

        /**
         * 深度图组：每在途帧一张，随交换链尺寸重建
         *
         * 声明顺序与创建顺序一致：深度图 → 描述符管理器 → 编译缓存 →
         * shadow pass → 前向 pass。正常析构（没有走 Destroy）时成员逆序释放，
         * 正好是「使用者先、被使用者后」
         */
        DoodleDepthImages m_depthImages;

        /**
         * 材质描述符管理器：按材质缓存 set 1（布局来自材质的着色器）；每帧常量由各 pass 自己持有
         *
         * 排在深度图之后（默认白图上传要命令层）、所有 pass 之前（pass 录制时绑它的集合）
         */
        DoodleDescriptorManager m_descriptorManager;

        /**
         * 驱动编译缓存：全进程一个，所有 pass 共用
         *
         * 排在描述符管理器之后、所有 pass 之前（建管线时要用它）
         */
        DoodleVulkanPipelineCompileCache m_pipelineCompileCache;

        /**
         * 待预热队列：着色器初始化成功后投递，本类在取帧之前排空并按 pass 分发
         *
         * 声明在所有资源成员之前：它不依赖任何其他成员，构造即登记、析构最晚。
         * 队列只存非拥有指针，Destroy 开头清空（见 Destroy 的注释）
         */
        DoodlePipelineWarmupQueue m_pipelineWarmupQueue;

        /**
         * 阴影 pass 骨架：本次只占编排位置
         *
         * 排在 forward 之前录；每帧常量的 UBO 已由基类按 shadow 的契约建出，但还不建
         * shadow map、不建管线。
         * 实现阴影时由它自己长成真正的深度 pass
         */
        DoodleShadowPass m_shadowPass;

        /**
         * 前向主 pass：拥有这一 pass 下的所有管线
         *
         * 排在 shadow pass 之后录；以后延迟、后处理各派生一个，由本类按顺序持有
         */
        DoodleForwardPass m_forwardPass;
    };
}
