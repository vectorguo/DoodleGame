//
//  DoodleSceneLoader.h
//  Doodle
//
//  Created by 郭智 on 2026/10/25.
//

#pragma once

#include <cstddef>
#include <map>
#include <span>
#include <string>
#include <string_view>

#include "Asset/DoodleAssetLoader.h"
#include "DoodleSceneData.h"

namespace Doodle
{
    /**
     * 场景文件 loader，资产管理器内部使用
     *
     * 对外只暴露 assetManager.Load<DoodleSceneData>(path)。本类负责把 JSON
     * 解析成 DoodleSceneData，并按路径缓存结果：同一个 key 重复 Load 返回
     * 同一个指针。
     *
     * 解析是纯函数（Parse），不碰场景、不碰 Vulkan；实例化 GameObject 是
     * DoodleScene::InitializeFromData 的事。这个分界让解析可以脱离设备测试，
     * 也让将来把解析放到工作线程时不需要改接口。
     */
    class DoodleSceneLoader final : public DoodleAssetLoader<DoodleSceneData>
    {
    public:
        DoodleSceneLoader() = default;
        ~DoodleSceneLoader() override = default;

        DoodleSceneLoader(const DoodleSceneLoader&) = delete;
        DoodleSceneLoader& operator=(const DoodleSceneLoader&) = delete;
        DoodleSceneLoader(DoodleSceneLoader&&) = delete;
        DoodleSceneLoader& operator=(DoodleSceneLoader&&) = delete;

        [[nodiscard]] const DoodleSceneData* Load(const DoodleAssetLoadContext& context,
                                                  std::string_view key) override;

        void Destroy() override;

        /**
         * JSON 字节 → 场景数据；纯函数，不读文件、不缓存
         *
         * @param sourceName 只用于错误消息，通常是文件路径
         * @throw std::runtime_error 语法错误、字段缺失、类型不对、未知字段、非有限数
         */
        [[nodiscard]] static DoodleSceneData Parse(std::span<const std::byte> bytes,
                                                   std::string_view sourceName);

    private:
        /**
         * 路径 → 场景数据。用 map 而不是 unordered_map：节点地址稳定，
         * Load 返回的指针不会因为后续插入失效；透明比较支持 string_view 直接查
         */
        std::map<std::string, DoodleSceneData, std::less<>> m_sceneData;
    };
}
