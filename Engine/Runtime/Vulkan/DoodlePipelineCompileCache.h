//
//  DoodlePipelineCompileCache.h
//  Doodle
//
//  Created by 郭智 on 2026/11/6.
//

#pragma once

#include <cstddef>
#include <vector>

#include <vulkan/vulkan_core.h>

namespace Doodle
{
    //只在接口签名与成员指针里出现，完整定义留给源文件
    class DoodleVulkanDevice;

    /**
     * 驱动编译缓存：一个 VkPipelineCache
     *
     * 和 DoodleRenderPass 不是一回事：那是我们自己写的、每个 pass 一张
     * 「按描述查找、创建并持有管线」的表；这一个属于驱动，缓存的是着色器的
     * 编译结果 —— 同样的着色器与状态再建一条管线（换个渲染状态、格式变了
     * 原地重建）时，驱动可以跳过编译。
     *
     * 全进程只有一个，由门面持有，所有 pass 共用，每次调用
     * vkCreateGraphicsPipelines 都传进去。建的时候不带
     * VK_PIPELINE_CACHE_CREATE_EXTERNALLY_SYNCHRONIZED_BIT，所以它是内部同步的：
     * 以后后台线程并行建管线，多个线程同时用它不需要外面再加锁。
     *
     * 存盘、跨次运行复用是已知必须做的一步：大世界的加载卡顿，尤其是 Android 上的
     * 管线编译卡顿，主要靠它解决。前提是平台层先有一个可写目录（Android 的
     * internalDataPath、macOS 的 Application Support、Windows 的 LocalAppData），
     * 而现在的 DoodleFileSystem 只能读，所以单独排期。到那一步时：读盘的字节从
     * Initialize 的 initialData 进来；写盘的字节由本类新增的方法用
     * vkGetPipelineCacheData 取出来。规范要求驱动在数据头（厂商 / 设备 / 缓存 UUID）
     * 对不上时把缓存当成空的，但有些 Android 驱动对损坏的数据不够健壮，
     * 读盘那一侧要先自己核对数据头再交给驱动。
     *
     * 生命周期：排在描述符管理器之后建、所有 pass 之后拆。拆掉它不影响
     * 已经建好的管线 —— 管线只在创建那一刻读它 —— 但 pass 的管线原地重建时
     * 还要用，所以让它活得比 pass 久。
     */
    class DoodlePipelineCompileCache
    {
    public:
        DoodlePipelineCompileCache() = default;

        //句柄的生死在本类手里：复制一份就是两个对象各自去销毁同一个句柄
        DoodlePipelineCompileCache(const DoodlePipelineCompileCache&) = delete;
        DoodlePipelineCompileCache& operator=(const DoodlePipelineCompileCache&) = delete;

        /**
         * 建驱动编译缓存
         *
         * @param device 已初始化的 Vulkan 设备
         * @param initialData 上一次运行存下来的缓存数据；空表示从零开始。
         *        现在总是传空（存盘还没做，见类注释）
         *
         * @note 前置条件：尚未初始化
         */
        void Initialize(const DoodleVulkanDevice& device, const std::vector<std::byte>& initialData);

        /**
         * 销毁驱动编译缓存
         *
         * 可以重复调用。前置条件：没有线程正在用它建管线
         */
        void Destroy();

        /**
         * 驱动编译缓存句柄，传给 vkCreateGraphicsPipelines
         */
        [[nodiscard]] VkPipelineCache GetPipelineCache() const
        {
            return m_pipelineCache;
        }

    private:
        /**
         * Vulkan设备层，在 Initialize 时绑定
         */
        const DoodleVulkanDevice* m_pDevice = nullptr;

        /**
         * 驱动编译缓存
         */
        VkPipelineCache m_pipelineCache = VK_NULL_HANDLE;
    };
}
