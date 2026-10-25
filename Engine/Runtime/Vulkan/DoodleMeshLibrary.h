//
//  DoodleMeshLibrary.h
//  Doodle
//
//  Created by 郭智 on 2026/10/24.
//

#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>

#include "DoodleMesh.h"

namespace Doodle
{
    /**
     * 网格库：网格资产的拥有者与名字索引
     *
     * 现在不再直接挂在 DoodleVulkanManager 下，而是作为
     * DoodleMeshAssetLoader 的内部成员，由 DoodleAssetManager 间接持有。
     * 寿命仍然跟着 Vulkan 设备走 —— 资产管理器在设备销毁之前释放它；
     * Android 上 Vulkan 是延迟初始化的，所以库也不能做成应用级静态量。
     * 它只在加载期上传数据，不参与每帧绘制。
     *
     * 现在只有内建的 "builtin:cube"；场景加载时资产管理器按名字来这里取。
     * Find 返回非拥有指针，寿命与库一致：库销毁后指针失效。
     *
     * 用 std::map + std::less<> 而不是 unordered_map：Find 收 string_view，
     * 透明比较让它直接查、不必构造一个临时的 std::string。网格数量以十计，
     * 这里不值得为「哈希更快」付一次分配的代价
     */
    class DoodleMeshLibrary
    {
    public:
        DoodleMeshLibrary() = default;

        DoodleMeshLibrary(const DoodleMeshLibrary&) = delete;
        DoodleMeshLibrary& operator=(const DoodleMeshLibrary&) = delete;

        /**
         * 建出内建网格并上传
         *
         * @param device 已初始化的 Vulkan 设备，且命令缓冲层的传输池已经建好
         *        —— 上传要用它录拷贝命令
         */
        void Initialize(const DoodleVulkanDevice& device);

        /**
         * 销毁库里的所有网格
         *
         * @note 前置条件：GPU 已空闲。由 DoodleVulkanManager 在 vkDeviceWaitIdle
         *       之后调用
         */
        void Destroy();

        /**
         * 按名字找网格
         *
         * @return 找不到时返回 nullptr；返回的是非拥有指针
         */
        [[nodiscard]] const DoodleMesh* Find(std::string_view name) const;

        /**
         * 内建正方体的名字，场景文件里引用的就是它
         */
        static constexpr std::string_view BuiltinCubeName = "builtin:cube";

    private:
        /**
         * 建出内建网格
         *
         * 内建资产目前只有一颗正方体；将来多起来时按名字逐个登记
         */
        void CreateBuiltinMeshes(const DoodleVulkanDevice& device);

    private:
        /**
         * 名字 → 网格。库是唯一拥有者，Find 交出去的是裸指针
         */
        std::map<std::string, std::unique_ptr<DoodleMesh>, std::less<>> m_meshes;
    };
}
