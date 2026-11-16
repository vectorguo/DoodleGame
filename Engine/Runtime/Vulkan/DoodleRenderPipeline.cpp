//
//  DoodleRenderPipeline.cpp
//  Doodle
//
//  Created by 郭智 on 2026/11/9.
//

#include "DoodleRenderPipeline.h"

#include "DoodlePipelineWarmupQueue.h"
#include "DoodleSwapChain.h"
#include "DoodleVulkanDevice.h"
#include "DoodleVulkanManager.h"
#include "Rendering/DoodleFrameContext.h"

namespace Doodle
{
    DoodleRenderPipeline* DoodleRenderPipeline::s_pInstance = nullptr;

    std::array<DoodleRenderPass*, 2> DoodleRenderPipeline::GetPasses()
    {
        //渲染顺序：阴影先、前向后。Destroy 逆序遍历这个数组
        return { &m_shadowPass, &m_forwardPass };
    }

    void DoodleRenderPipeline::Initialize(const DoodleVulkanManager& vulkanManager)
    {
        //只从后端取本次初始化需要的两样；以后要用新的后端服务，也从这个参数取，
        //Initialize 的签名与调用点不再变
        const DoodleVulkanDevice& device = vulkanManager.GetDevice();
        const DoodleSwapChain& swapChain = vulkanManager.GetSwapChain();

        m_pDevice = &device;
        m_pSwapChain = &swapChain;

        //启动失败时收敛到空状态：Destroy 对每一层都是幂等的，
        //避免半截资源留在渲染管理器外面
        try
        {
            //深度图按交换链尺寸建。前向 pass 只吃它的格式，不吃对象本身
            m_depthImages.Initialize(device, swapChain.GetImageExtent());

            //描述符管理器只建材质纹理的布局/池/集合与默认白图；
            //默认白图的上传借命令管理层的传输池 —— 门面必须先初始化命令管理层
            m_descriptorManager.Initialize(device);

            //驱动编译缓存先建：每次建管线都把它传给 vkCreateGraphicsPipelines
            m_pipelineCompileCache.Initialize(device, {});

            //shadow 目前只是骨架：不建 GPU 资源，也不录任何命令
            m_shadowPass.Initialize(
            {
                .renderPassInfo =
                {
                    .device = device,
                    .pipelineCompileCache = m_pipelineCompileCache.GetPipelineCache()
                }
            });

            //前向 pass：自己建 set 0 常量资源，再按附件配置与编译缓存建管线表
            m_forwardPass.Initialize(
            {
                .renderPassInfo =
                {
                    .device = device,
                    .pipelineCompileCache = m_pipelineCompileCache.GetPipelineCache()
                },
                .swapChain = swapChain,
                .depthImages = m_depthImages
            });
        }
        catch (...)
        {
            Destroy();
            throw;
        }
    }

    void DoodleRenderPipeline::Destroy()
    {
        //待预热队列里是着色器的非拥有指针，资产可能已经先于本类销毁。
        //这里只丢指针、不解引用；放在早退之前，从未初始化的幂等路径也走一次
        m_pipelineWarmupQueue.Clear();

        if (m_pDevice == nullptr)
        {
            return;
        }

        //按「使用者先、被使用者后」拆；pass 按渲染顺序的逆序走。
        //
        //pass 先走：它们的管线布局引用材质 set 1 布局与各自的 set 0 布局，
        //常量资源也由各自拆；接着驱动编译缓存；描述符管理器释放材质池与默认白图；
        //深度图不依赖上面的任何一层，最后走
        const std::array<DoodleRenderPass*, 2> passes = GetPasses();
        for (auto it = passes.rbegin(); it != passes.rend(); ++it)
        {
            (*it)->Destroy();
        }
        m_pipelineCompileCache.Destroy();
        m_descriptorManager.Destroy();
        m_depthImages.Destroy();

        m_pSwapChain = nullptr;
        m_pDevice = nullptr;
    }

    void DoodleRenderPipeline::Render(const DoodleFrameContext& frameContext)
    {
        //材质描述符集每帧只解析一次：set 1 按绘制项顺序与 drawItems 一一对应；
        //返回的引用活到下一次解析，正好覆盖本次录制。帧索引 / 图像索引 /
        //命令缓冲 / 绘制项都在 frameContext 里，不重复放进 passContext
        const DoodleRenderPassContext passContext
        {
            .materialDescriptorSets = m_descriptorManager.ResolveMaterialDescriptorSets(frameContext.drawItems)
        };

        //按渲染顺序把每个 pass 录进同一条命令缓冲；pass 自己的附件与每帧常量
        //由各自的 pass 负责
        for (DoodleRenderPass* pPass : GetPasses())
        {
            pPass->Render(frameContext, passContext);
        }
    }

    void DoodleRenderPipeline::WarmupPipelines()
    {
        //稳态队列为空，本函数只是一次加锁与一次判空 —— 成本不随绘制项数量增长
        const std::vector<const DoodleShader*> pendingShaders = m_pipelineWarmupQueue.Drain();
        if (pendingShaders.empty())
        {
            return;
        }

        //按渲染顺序逐个分发 —— 本函数只负责排空与分发，
        //每个 pass 自己决定要什么管线
        for (DoodleRenderPass* pPass : GetPasses())
        {
            pPass->WarmupShaders(pendingShaders);
        }
    }

    void DoodleRenderPipeline::OnSurfaceLost()
    {
        //深度图按交换链尺寸分配，表面没了就失去尺寸依据。
        //管线、per-pass 常量与描述符资源都与表面无关，保留下来 —— 重建它们代价太大
        m_depthImages.Destroy();
    }

    void DoodleRenderPipeline::OnSwapChainRecreated()
    {
        //旧深度图按旧尺寸分配，先拆再建。Destroy 幂等：
        //OnSurfaceLost 刚拆过、或交换链只是尺寸变了，两条路都能安全走到这里
        m_depthImages.Destroy();
        m_depthImages.Initialize(*m_pDevice, m_pSwapChain->GetImageExtent());

        //尺寸变化不影响管线（视口与剪裁是动态状态）；只有颜色 / 深度格式或
        //采样数真的变了，前向 pass 才原地重建自己的管线。判断由它自己做
        m_forwardPass.RebuildIfPassConfigChanged(*m_pSwapChain, m_depthImages);
    }

}
