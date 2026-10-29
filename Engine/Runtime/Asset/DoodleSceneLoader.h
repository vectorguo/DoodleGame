//
//  DoodleSceneLoader.h
//  Doodle
//
//  Created by 郭智 on 2026/10/25.
//

#pragma once

#include <cstddef>
#include <memory>
#include <span>
#include <string_view>

#include "DoodleAssetLoader.h"
#include "Scene/DoodleSceneData.h"

namespace Doodle
{
    /**
     * 场景文件 loader，资产管理器内部使用
     *
     * 对外只暴露 assetManager.Load<DoodleSceneData>(path)。缓存、去重与销毁在
     * DoodleAssetLoader<DoodleSceneData> 基类里；本类只负责把路径读成场景数据，
     * 「同一个 key 重复 Load 返回同一个指针」由基类保证。
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

        /**
         * JSON 字节 → 场景数据；纯函数，不读文件、不缓存
         *
         * @param sourceName 只用于错误消息，通常是文件路径
         * @throw std::runtime_error 语法错误、字段缺失、类型不对、未知字段、非有限数
         */
        [[nodiscard]] static DoodleSceneData Parse(std::span<const std::byte> bytes, std::string_view sourceName);

    protected:
        [[nodiscard]] std::unique_ptr<DoodleSceneData> Create(const DoodleAssetLoadContext& context, std::string_view key) override;
    };
}
