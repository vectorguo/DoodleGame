//
//  DoodleRenderManager.cpp
//  Doodle
//
//  Created by 郭智 on 2026/11/9.
//

#include "DoodleRenderManager.h"

#include "../Platform/DoodleWindow.h"
#include "Rendering/DoodleCamera.h"

namespace Doodle
{
    DoodleRenderManager* DoodleRenderManager::s_pInstance = nullptr;

    void DoodleRenderManager::Initialize(DoodleWindow& window)
    {
        //后端先建出设备、交换链与命令层
        m_vulkanManager.Initialize(window);

        //渲染管线的描述符管理器要传默认白图，借后端命令层的传输池。
        m_renderPipeline.Initialize(m_vulkanManager);
    }

    void DoodleRenderManager::Destroy()
    {
        //等空闲放在这里，因为渲染管线自己不会等
        m_vulkanManager.WaitForGpuIdle();

        //渲染管线的深度图、描述符、管线必须先于设备释放；
        m_renderPipeline.Destroy();
        m_vulkanManager.Destroy();
    }

    void DoodleRenderManager::Render(const std::vector<DoodleDrawItem>& drawItems)
    {
        // 先排空待预热队列：着色器资产在初始化成功时投递，这里按 pass 把它们的
        // 管线建出来。输入不是绘制项 —— 每个着色器在每个 pass 里只有一条管线，
        // 跟本帧画了多少东西无关；队列为空时本调用只是一次判空。
        //
        // 位置在取主相机之前、BeginFrame 之前：建管线不需要相机，也不需要交换链
        // 图像。加载阶段即使还没有主相机，管线也应该在加载边界编完，而不是攒到
        // 第一帧真正有画面时再一次性爆发；建失败也发生在还没有获取图像的时候。
        m_renderPipeline.WarmupPipelines();

        // 主相机由 DoodleCamera 自己维护，这里只取结果。没有相机就没有
        // view / proj：跳过本帧，而不是拿一个假相机去画。绘制项已经由调用方
        // 收集好传进来，本层不决定要不要收集。启动场景已经在 LoadStartupScene
        // 里校验过必须有一台，这里为空只可能是运行期把相机组件停用或销毁了
        const DoodleCamera* pMainCamera = DoodleCamera::GetMainCamera();
        if (pMainCamera == nullptr)
        {
            return;
        }

        // 相机、光源与绘制项在本帧组成一份上下文；后端会在 BeginFrame 里
        // 填它的状态字段（status / frameIndex / imageIndex / commandBuffer）。
        // drawItems 是调用方容器的引用，本层不拥有它，Render 返回后即可复用
        DoodleFrameContext frameContext(pMainCamera, drawItems);

        //帧时序与边界条件全在后端。这里只按状态分支：
        //- Skipped：表面被 OnSurfaceLost 拆掉，本帧什么都不做；
        //- RecreateSwapChain：acquire 时发现交换链过时，放弃本帧
        auto status = m_vulkanManager.BeginFrame(frameContext);
        if (status == EFrameStatus::Ready)
        {
            //命令缓冲已经 begin；录制内容（UBO、描述符、pass 的附件与绘制）
            //由渲染管线决定。提交与呈现归后端
            m_renderPipeline.Render(frameContext);
            status = m_vulkanManager.EndFrame(frameContext);
        }

        if (status == EFrameStatus::RecreateSwapChain)
        {
            RecreateSwapChain();
        }
    }

    void DoodleRenderManager::WaitForGpuIdle()
    {
        m_vulkanManager.WaitForGpuIdle();
    }

    void DoodleRenderManager::OnSurfaceLost()
    {
        //后端先等 GPU 空闲，拆同步对象、交换链与表面；
        //渲染管线的深度图与表面无关，随后拆掉即可
        m_vulkanManager.OnSurfaceLost();
        m_renderPipeline.OnSurfaceLost();
    }

    void DoodleRenderManager::OnSurfaceRecreated(DoodleWindow& window)
    {
        //后端只恢复表面与队列选择；交换链、同步对象与渲染管线的深度图 / pass
        //交给 RecreateSwapChain 一条路恢复
        m_vulkanManager.OnSurfaceRecreated(window);
        RecreateSwapChain();
    }

    void DoodleRenderManager::RecreateSwapChain()
    {
        //后端先重建同步对象与交换链；窗口正在关闭时它返回 false，本函数什么都不做
        if (m_vulkanManager.RecreateSwapChain())
        {
            //交换链与同步对象已经换新；渲染管线按新尺寸重建深度图，
            //并让前向 pass 在附件格式真的变化时原地重建管线
            m_renderPipeline.OnSwapChainRecreated();
        }
    }
}
