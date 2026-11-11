//
//  DoodleRenderPipeline.cpp
//  Doodle
//
//  Created by 郭智 on 2026/11/9.
//

#include "DoodleRenderPipeline.h"

#include "DoodleSwapChain.h"
#include "DoodleVulkanDevice.h"
#include "DoodleVulkanManager.h"
#include "DoodleVulkanUtils.h"
#include "Rendering/DoodleCamera.h"
#include "Rendering/DoodleFrameContext.h"
#include "Rendering/DoodleLight.h"
#include "Rendering/DoodleShaderData.h"

namespace Doodle
{
    void DoodleRenderPipeline::Initialize(const DoodleVulkanManager& vulkanManager)
    {
        //只从后端取本次初始化需要的两样；以后要用新的后端服务，也从这个参数取，
        //Initialize 的签名与调用点不再变
        const DoodleVulkanDevice& device = vulkanManager.GetDevice();
        const DoodleSwapChain& swapChain = vulkanManager.GetSwapChain();

        m_pDevice = &device;
        m_pSwapChain = &swapChain;

        //深度图按交换链尺寸建。前向 pass 只吃它的格式，不吃对象本身
        m_depthImages.Initialize(device, swapChain.GetImageExtent());

        //UBO 每帧更新，走 HostVisible 类型（CPU 直接写），不走设备本地 + 中转。
        //映射由第一次 Write 建立，这里只负责分配
        CreateFrameUbos();

        //描述符管理器要写 UBO 缓冲句柄，并建出两个 set 布局。
        //默认白图的上传借命令管理层的传输池 —— 门面必须先初始化命令管理层
        m_descriptorManager.Initialize(device, m_frameUbos);

        //驱动编译缓存先建：每次建管线都把它传给 vkCreateGraphicsPipelines
        m_pipelineCompileCache.Initialize(device, {});

        //前向 pass：绑定 frame UBO 的 set 0 布局、附件配置与驱动编译缓存，此时还没有管线
        m_forwardPass.Initialize(
        {
            .renderPassInfo =
            {
                .device = device,
                .pipelineCompileCache = m_pipelineCompileCache.GetPipelineCache(),
                .frameUboDescriptorSetLayout = m_descriptorManager.GetFrameUboDescriptorSetLayout()
            },
            .swapChain = swapChain,
            .depthImages = m_depthImages,
            .materialDescriptorSetLayout = m_descriptorManager.GetMaterialDescriptorSetLayout()
        });
    }

    void DoodleRenderPipeline::Destroy()
    {
        if (m_pDevice == nullptr)
        {
            return;
        }

        //按「使用者先、被使用者后」拆。
        //
        //前向 pass 先走：它的管线布局引用描述符管理器的两个 set 布局；
        //接着驱动编译缓存；描述符管理器释放两组池与默认白图后，UBO 才能销毁；
        //深度图不依赖上面的任何一层，最后走
        m_forwardPass.Destroy();
        m_pipelineCompileCache.Destroy();
        m_descriptorManager.Destroy();
        DestroyFrameUbos();
        m_depthImages.Destroy();

        m_pSwapChain = nullptr;
        m_pDevice = nullptr;
    }

    void DoodleRenderPipeline::Render(const DoodleFrameContext& frameContext)
    {
        //先写 UBO：位置必须在栅栏之后、录制之前
        UpdateFrameUbo(frameContext.frameIndex, frameContext);

        //组好本 pass 的管线侧资源。描述符集合在这里解析：set 1 按绘制项顺序与
        //drawItems 一一对应；返回的引用活到下一次解析，正好覆盖本次录制。
        //帧索引 / 图像索引 / 命令缓冲 / 绘制项都在 context 里，不重复放进 passContext
        const DoodleRenderPassContext passContext
        {
            .swapChain = *m_pSwapChain,
            .depthImages = m_depthImages,
            .frameUboDescriptorSet = m_descriptorManager.GetFrameUboDescriptorSet(frameContext.frameIndex),
            .materialDescriptorSets = m_descriptorManager.ResolveMaterialDescriptorSets(frameContext.drawItems)
        };

        //现在只有前向 pass。以后按顺序把多个 pass 录进同一条命令缓冲；
        //pass 之间的附件转换由本类编排，第二个 pass 到来时再引入渲染图
        m_forwardPass.Render(frameContext, passContext);
    }

    void DoodleRenderPipeline::OnSurfaceLost()
    {
        //深度图按交换链尺寸分配，表面没了就失去尺寸依据。
        //管线、UBO 与描述符资源都与表面无关，保留下来 —— 重建它们代价太大
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

    void DoodleRenderPipeline::CreateFrameUbos()
    {
        //每帧一块，不允许共用：在途帧的 GPU 可能还在读上一轮，
        //host 这边却已经开始写下一轮的矩阵
        for (auto& buffer : m_frameUbos)
        {
            //只声明 UNIFORM_BUFFER 用途，不需要 TRANSFER_DST：
            //数据由 CPU 直接写，不经过拷贝命令。
            //这里只分配、不配置映射：第一次 Write 时才建立持久映射
            buffer.Initialize(
                *m_pDevice,
                sizeof(DoodleUniformBufferObject),
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                DoodleGraphicsBuffer::EMemoryType::HostVisible);
        }
    }

    void DoodleRenderPipeline::DestroyFrameUbos()
    {
        //Destroy 内部会先解除持久映射，再销毁 buffer/memory
        for (auto& buffer : m_frameUbos)
        {
            buffer.Destroy();
        }
    }

    void DoodleRenderPipeline::UpdateFrameUbo(const uint32_t frameIndex, const DoodleFrameContext& context)
    {
        DoodleUniformBufferObject ubo;

        //view 由相机从宿主 Transform 现算：相机不存位置 / 朝向，
        //移动它所在的对象，画面自然跟着变
        ubo.view = context.camera.GetViewMatrix();

        //每帧重新读交换链尺寸：窗口缩放后宽高比自动正确，
        //描述符集与 UBO 对象本身都不需要重建
        const auto extent = m_pSwapChain->GetImageExtent();
        const auto preTransform = m_pSwapChain->GetPreTransform();

        //宽高比要用**用户看到的那一组**。交换链做了预旋转时，图像宽高和显示方向是对调的，
        //所以这里再对调回来 —— 透视必须按显示方向算，算完再转，才是同一份画面。
        //
        //Y 翻转已经在 GetProjectionMatrix 里做完；这里拿到的 proj 是「显示方向 +
        //Vulkan 裁剪空间」的结果
        ubo.proj = context.camera.GetProjectionMatrix(DoodleVulkanUtils::GetDisplayAspect(extent, preTransform));

        //最后把整幅画面转到图像自己的朝向上。桌面拿到的是 IDENTITY，这一乘是单位阵。
        //
        //必须排在 Y 翻转之后、作为投影的最后一步左乘：Y 翻转负责换成 Vulkan 的 NDC，
        //预旋转是在 Vulkan 的 NDC 里做的。顺序一换，Android 上会静默变形
        ubo.proj = DoodleVulkanUtils::MakePreRotation(preTransform) * ubo.proj;

        //主光源：方向取组件宿主 Transform 的前向，颜色乘上强度。
        //方向已经是单位向量（DoodleLight::GetDirection 负责），这里不再归一化
        //
        //没有启用的光源时写零颜色：片元着色器只剩环境项，画面偏暗但仍是
        //同一份着色逻辑。方向填一个固定值只是让 UBO 内容确定，不参与可见结果
        if (context.pLight != nullptr)
        {
            ubo.lightDirection = glm::vec4(context.pLight->GetDirection(), 0.0f);
            ubo.lightColor = glm::vec4(context.pLight->GetColor() * context.pLight->GetIntensity(), 0.0f);
        }
        else
        {
            ubo.lightDirection = glm::vec4(0.0f, 0.0f, -1.0f, 0.0f);
            ubo.lightColor = glm::vec4(0.0f);
        }

        //HOST_COHERENT 保证不需要手动 flush；下一次 vkQueueSubmit 之后再读取时，
        //这份写入对 GPU 可见。写入位置已在调用处保证：栅栏之后、录制之前。
        //第一次调用会在缓冲内部建立持久映射，之后每帧只是一次 memcpy
        //
        //这里只有帧级数据（相机 + 光源）。每个绘制项的 localToWorld 走 push constant，
        //由 DoodleForwardPass::Render 在逐个 draw item 时推进命令缓冲
        m_frameUbos[frameIndex].Write(&ubo, sizeof(ubo));
    }

}
