//
//  DoodleDepthImages.h
//  Doodle
//
//  Created by 郭智 on 2026/10/17.
//

#pragma once

#include <array>
#include <vulkan/vulkan_core.h>

#include "DoodleImage.h"
#include "DoodleVulkanConfig.h"

namespace Doodle
{
    //只在接口签名与成员指针里出现，完整定义留给源文件
    class DoodleVulkanDevice;

    /**
     * 深度图组：每个在途帧一张，按 frameIndex 取用
     *
     * 建图/分配/视图的手续在 DoodleImage 里，本类只留两件深度特有的事：
     * - **格式策略**：候选表、模板分量的判断
     * - **按帧轮转**：数量与命令缓冲、imageAvailable 信号量、帧栅栏同一个维度
     *
     * 按帧分配（而不是所有在途帧共用一张）：同一槽位下一次被用到之前，它上一次的
     * 使用必定已经完成 —— 凭据是 DrawFrame 开头等的那个帧栅栏。所以这一组图
     * 不需要任何跨帧屏障；每帧那条屏障只负责转布局
     *
     * 寿命锚在「窗口尺寸」上：交换链重建时由门面一并重建。
     * 但它不是交换链的一部分 —— 不是呈现目标、不与呈现引擎打交道，
     * 也不随交换链格式变化
     *
     * 创建完成后处于 VK_IMAGE_LAYOUT_UNDEFINED：布局转换在每帧的屏障里做
     */
    class DoodleDepthImages
    {
    public:
        DoodleDepthImages() = default;

        //句柄的成对生死在本类手里：复制一份就是两个对象各自去销毁同一组句柄
        DoodleDepthImages(const DoodleDepthImages&) = delete;
        DoodleDepthImages& operator=(const DoodleDepthImages&) = delete;

        /**
         * 建出每帧一张的深度图
         *
         * @param device 已初始化的Vulkan设备，取逻辑设备、物理设备与内存类型用
         * @param extent 深度图尺寸，取交换链图像尺寸 —— 必须与颜色附件逐像素对齐
         *
         * @note 前置条件：尚未初始化。重建时请走 Destroy 后重新 Initialize
         */
        void Initialize(const DoodleVulkanDevice& device, VkExtent2D extent);

        /**
         * 销毁整组深度图
         *
         * @note 前置条件：GPU 已空闲。图可能还在被在途的命令引用着，
         * 此刻销毁是未定义行为 —— 由 DoodleVulkanManager 的 vkDeviceWaitIdle 保证
         */
        void Destroy();

        /**
         * 获取第 frameIndex 张深度图的视图
         *
         * 命令缓冲把它写进 VkRenderingAttachmentInfo，每帧现取
         *
         * @param frameIndex 当前在途帧索引，见 DoodleVulkanManager::m_currentFrame
         */
        [[nodiscard]] VkImageView GetImageView(const uint32_t frameIndex) const
        {
            return m_images[frameIndex].GetImageView();
        }

        /**
         * 获取第 frameIndex 张深度图
         *
         * 布局转换的屏障要它
         */
        [[nodiscard]] VkImage GetImage(const uint32_t frameIndex) const
        {
            return m_images[frameIndex].GetImage();
        }

        /**
         * 获取整组共用的深度格式
         *
         * 图形管线把它声明进 VkPipelineRenderingCreateInfo；屏障用它算切面。
         * 一组图共用一个格式：同一种用途，没有逐帧不同的理由
         */
        [[nodiscard]] VkFormat GetFormat() const
        {
            return m_depthFormat;
        }

        /**
         * 判断一个深度格式是否含模板分量
         *
         * 公开的静态：它有两个外部调用点，各自要做一个不同的分支 ——
         * 管线声明 stencilAttachmentFormat 填不填、屏障的 aspectMask 加不加模板位。
         * 同一个判据被两处需要，说明它是这一类的公共知识，不是私有细节
         */
        [[nodiscard]] static bool HasStencilComponent(VkFormat format)
        {
            return format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT;
        }

    private:
        /**
         * 挑一个既支持深度附件、又含深度分量的格式
         *
         * 候选表按偏好从高到低排：32 位浮点首选，另外两个带模板的兜底。
         * 全都不支持时抛出 —— 那是配置问题，不是可以继续跑的情形
         */
        [[nodiscard]] VkFormat FindDepthFormat() const;

    private:
        /**
         * Vulkan设备层，在 Initialize 时绑定，取物理设备查格式用
         */
        const DoodleVulkanDevice* m_pDevice = nullptr;

        /**
         * 整组共用的深度格式
         */
        VkFormat m_depthFormat = VK_FORMAT_UNDEFINED;

        /**
         * 每帧一张，按 frameIndex 取用。
         * 数量取 MAX_FRAMES_IN_FLIGHT，与命令缓冲、帧栅栏同一个维度
         */
        std::array<DoodleImage, MAX_FRAMES_IN_FLIGHT> m_images;
    };
}
