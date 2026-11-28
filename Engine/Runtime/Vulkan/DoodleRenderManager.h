//
//  DoodleRenderManager.h
//  Doodle
//
//  Created by 郭智 on 2026/11/9.
//

#pragma once

#include "Utility/DoodleStd.h"

#include "DoodleRenderPipeline.h"
#include "DoodleVulkanManager.h"

namespace Doodle
{
    //只按引用持有窗口，完整定义留给源文件去取
    class DoodleWindow;
    class DoodleApplication;
    struct DoodleDrawItem;
    struct DoodleFrameContext;

    /**
     * 渲染管理器：应用面对的渲染入口，也是后端与渲染管线的组合根
     *
     * 持有两个平级的子对象：
     * - DoodleVulkanManager：设备、交换链、命令层、同步对象与帧时序；
     * - DoodleRenderPipeline：深度图、材质描述符管理器、驱动编译缓存与 pass。
     *
     * 本类只做编排：取主相机 / 主光源组帧上下文 → 开始一帧 → 交给渲染管线录
     * → 提交呈现；以及表面生命周期、交换链重建时两边的调用顺序。**帧时序的边界条件
     * 全部留在后端** —— 栅栏复位、acquire 失败时信号量状态、present 后重建、
     * resize 标志都由 DoodleVulkanManager 的 BeginFrame / EndFrame /
     * RecreateSwapChain 处理，本类只按返回状态分支，不重写任何 Vulkan 细节。
     *
     * 应用层仍然只面对这一个入口；两个子对象是本类的私有成员，各自另有受限的
     * GetInstance()（后端原语 / 渲染策略内部用），不改变本类作为应用入口的地位。
     * 生命周期与程序一致。
     *
     * 命名约定：整帧或一批对象的入口用 Render；单个对象的绘制用
     * DrawMesh / DrawCube 这类名字。
     */
    class DoodleRenderManager
    {
        friend class DoodleApplication;

        /**
         * 只能由 DoodleApplication 创建：运行时只有一份，实例由应用独占持有。
         * 构造即登记为当前实例，析构时清空
         */
        DoodleRenderManager()
        {
            s_pInstance = this;
        }

    public:
        ~DoodleRenderManager()
        {
            if (s_pInstance == this)
            {
                s_pInstance = nullptr;
            }
        }

        DoodleRenderManager(const DoodleRenderManager&) = delete;
        DoodleRenderManager& operator=(const DoodleRenderManager&) = delete;

        /**
         * 当前那一份渲染管理器
         *
         * 有效期与对象同寿（应用构造 → 应用析构）；「能用了」要到 Initialize
         * 成功之后、Destroy 之前。Initialize 之前 / Destroy 之后取到的是空壳，
         * 与 DoodleCommandManager 的口径一致。
         *
         * 需要设备 / 交换链原语的代码用 DoodleVulkanManager::GetInstance()；
         * 本入口只负责应用级的渲染动作（等 GPU 空闲、表面生命周期）
         */
        [[nodiscard]] static DoodleRenderManager& GetInstance()
        {
            return *s_pInstance;
        }

        /**
         * 初始化后端与渲染管线
         *
         * 顺序不能反：后端先建出设备、交换链与命令层；渲染管线的描述符管理器
         * 在上传默认白图时要借命令层的传输池
         *
         * @param window 提供表面的窗口；只借不放
         */
        void Initialize(DoodleWindow& window);

        /**
         * 销毁渲染管线与后端
         *
         * 顺序：等 GPU 空闲 → 渲染管线 → 后端。渲染管线的深度图 / 描述符 / 管线
         * 必须先于设备释放；后端自己的 Destroy 还会再等一次空闲
         */
        void Destroy();

        /**
         * 渲染并呈现一帧
         *
         * 固定顺序：排空待预热队列 → 取主相机 → 主光源组帧上下文 → 后端 BeginFrame
         * 填状态 → 渲染管线 Render → 后端 EndFrame 提交并呈现。任何一步报告交换链
         * 需要重建，本函数负责把两边一起恢复。
         *
         * 预热放在取主相机之前：加载阶段即使还没有主相机，管线也应该在加载边界
         * 编完，而不是攒到第一帧真正有画面时再一次性爆发。建管线不需要相机，
         * 也不需要交换链图像。
         *
         * drawItems 由调用方（应用层）从场景收集好后传入；相机与主光源由本类
         * 直接向 DoodleCamera / DoodleLight 取，调用方不再构造 DoodleFrameContext。
         * 没有主相机时直接跳过本帧 —— 没有 view / proj，画不出有效画面。
         * 帧上下文是本函数的栈上局部量，后端在 BeginFrame 里填状态字段；
         * 各成员的可空性与生命周期见 DoodleFrameContext 的注释
         *
         * @param drawItems 本帧的绘制项；数组顺序就是绘制顺序，为空合法（只清屏）
         *
         * @note 里面的 Vulkan 调用大多是异步的，返回不代表 GPU 已完成
         */
        void Render(const std::vector<DoodleDrawItem>& drawItems);

        /**
         * 等待 GPU 空闲
         *
         * 给「设备资源在渲染器之外销毁」的调用方用：应用销毁资产管理器之前
         * 必须先确保没有在途命令还在引用网格 / 纹理缓冲。本类自己的 Destroy
         * 也会等，但那已经排在资产管理器之后，等不到了
         *
         * @note 返回不代表任何排队中的工作已被取消，只是都完成了
         */
        void WaitForGpuIdle();

        /**
         * 表面没了：先停后端，再拆渲染管线跟随表面的资源
         *
         * Android 上切后台、锁屏、旋转都会走这条路。后端会先等 GPU 空闲，
         * 再拆同步对象、交换链与表面；渲染管线随后拆掉深度图。
         *
         * @note 期间不得调用 Render
         */
        void OnSurfaceLost();

        /**
         * 表面回来了：恢复后端表面，再重建交换链与渲染管线的深度图 / pass
         *
         * @param window 提供新的表面
         * @note 与 OnSurfaceLost 必须成对。可反复调用（每次切前后台一轮）
         */
        void OnSurfaceRecreated(DoodleWindow& window);

        /**
         * 通知窗口尺寸发生了变化，下一帧需要重建交换链
         *
         * 由上层在 GLFW 的 resize 回调里调用；只记标志，把处理留给帧边界
         */
        void OnWindowResized()
        {
            m_vulkanManager.OnWindowResized();
        }

        /**
         * 获取Vulkan设备
         *
         * 资产系统初始化与资源上传需要它；渲染器仍持有设备的生死
         */
        [[nodiscard]] DoodleVulkanDevice& GetDevice()
        {
            return m_vulkanManager.GetDevice();
        }

        /**
         * 获取Vulkan设备
         */
        [[nodiscard]] const DoodleVulkanDevice& GetDevice() const
        {
            return m_vulkanManager.GetDevice();
        }

    private:
        /**
         * 重建交换链，以及渲染管线跟随交换链的深度图 / 附件格式
         *
         * 后端先完成同步对象与交换链的重建；成功后渲染管线自行销毁并重建
         * 深度图，再让前向 pass 按新的附件格式决定要不要原地重建管线。
         *
         * 窗口正在关闭时后端返回 false，本函数什么都不做
         */
        void RecreateSwapChain();

    private:
        /**
         * 当前实例，构造时登记、析构时清空
         */
        static DoodleRenderManager* s_pInstance;

        /**
         * Vulkan 后端：设备、交换链、命令层、同步对象与帧时序
         *
         * 声明在前：渲染管线初始化时要借它的设备、交换链与命令层
         */
        DoodleVulkanManager m_vulkanManager;

        /**
         * 渲染管线：pass 的持有者与渲染资源的归属
         *
         * 声明在后：正常析构时它先于后端释放，正好是「使用者先、被使用者后」
         */
        DoodleRenderPipeline m_renderPipeline;

    };
}
