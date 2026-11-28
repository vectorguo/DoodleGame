//
//  DoodleFrameContext.h
//  Doodle
//
//  Created by 郭智 on 2026/10/30.
//

#pragma once

#include "Utility/DoodleStd.h"

#include <vulkan/vulkan_core.h>

#include "DoodleDrawItem.h"

namespace Doodle
{
    //只以引用 / 指针出现，完整定义留给使用它们的源文件
    class DoodleCamera;
    class DoodleLight;

    /**
     * 一帧的起始状态
     *
     * Ready：命令缓冲已 begin，可以交给渲染管线录制，之后 EndFrame 提交。
     * Skipped：当前没有可呈现的目标（表面被 OnSurfaceLost 拆掉），本帧什么都不做。
     * RecreateSwapChain：交换链与窗口对不上了，本帧放弃，调用方应重建后再来。
     */
    enum class EFrameStatus
    {
        Ready,
        Skipped,
        RecreateSwapChain,
    };

    /**
     * 一次 Render（一帧）的输入与状态
     *
     * 前三个字段由 DoodleRenderManager::Render 填入：主相机、绘制项与主光源。
     * 后四个字段是 DoodleVulkanManager::BeginFrame 填写的帧状态；只有
     * status == Ready 时，frameIndex / imageIndex / commandBuffer 才有效。
     *
     * 一个对象贯穿渲染管理器 → 后端 → 渲染管线：DoodleRenderManager 构造时
     * 填输入，后端在 BeginFrame 里补状态，渲染管线读同一份数据。这样不再单独
     * 维护 FrameState，也不用把状态从别处复制进来。代价是 Rendering 头文件
     * 现在会带 Vulkan 类型，这是刻意的取舍。
     *
     * 工程里 Context 的约定是「一次运行时操作的输入」：整帧的输入与状态都收在
     * FrameContext；单个 pass 的输入用 DoodleRenderPassContext；初始化输入用
     * ...InitInfo。
     *
     * DoodleRenderManager::Render 只从调用方收绘制项；相机与主光源由它自己取，
     * 状态由后端填。以后帧级输入继续长时，往本结构加字段，公开的 Render 签名
     * 仍然只认绘制项。
     *
     * 成员都是非拥有引用 / 指针，只在本次 Render 期间有效：
     * - camera 必须有效：view / proj 由渲染管线从它现算；
     * - pLight 可为空：没有启用的光源不是错误，着色器只剩环境项；
     * - drawItems 为空合法：本帧只清屏；
     * - 四个状态字段由后端填写，构造时保持默认值即可。
     *
     * camera 与 drawItems 用引用、pLight 用指针，是为了让「必须存在」和
     * 「可能缺席」在类型上分开。相机和绘制列表每帧都有，让它们可空只会给
     * 每个使用点加一句永远为真的判断；光源是唯一真会缺席的那一个。
     *
     * 这个结构是调用点的栈上临时量，不保存、不跨帧复用 ——
     * 它只是「本帧借用什么 + 本帧走到哪一步」的一组数据，没有自己的生命周期。
     */
    struct DoodleFrameContext
    {
    public:
        DoodleFrameContext(const DoodleCamera* pCamera, const std::vector<DoodleDrawItem>& drawItems);

    public:
        /**
         * 本帧的相机
         */
        const DoodleCamera& camera;

        /**
         * 本帧要画的网格与模型矩阵；数组顺序就是绘制顺序
         */
        const std::vector<DoodleDrawItem>& drawItems;

        /**
         * 本帧的主光源；为空表示没有启用的光源
         */
        const DoodleLight* pLight = nullptr;

        /**
         * 本帧的状态；由 BeginFrame 填写，只有 Ready 时后面三项帧数据有效
         */
        EFrameStatus status = EFrameStatus::Skipped;

        /**
         * 在途帧索引，与 UBO / 深度图 / 命令缓冲同一个索引；由 BeginFrame 填写
         */
        uint32_t frameIndex = 0;

        /**
         * 本帧交换链图像索引，颜色附件与呈现靠它；由 BeginFrame 填写
         */
        uint32_t imageIndex = 0;

        /**
         * 已经 begin、等待录制的命令缓冲；由 BeginFrame 填写
         */
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    };
}
