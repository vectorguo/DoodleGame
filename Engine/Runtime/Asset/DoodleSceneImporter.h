//
//  DoodleSceneImporter.h
//  Doodle
//
//  Created by 郭智 on 2026/10/29.
//

#pragma once

#include <cstddef>
#include <span>
#include <string_view>

#include "Scene/DoodleSceneData.h"

namespace Doodle
{
    /**
     * 场景文件的格式导入器
     *
     * 只做「JSON 字节 → DoodleSceneData」：不读文件、不缓存、不碰 Vulkan /
     * 场景 / 资产管理器。文件来源、缓存、去重与销毁归 DoodleSceneLoader；
     * 把数据装配成 GameObject 归 DoodleScene::InitializeFromData。
     *
     * 和 DoodleFbxImporter 是同一层：格式解析与资产所有权分开。这样解析可以
     * 脱离设备单测，工作线程化时只搬 importer，不动 loader。
     */
    class DoodleSceneImporter final
    {
    public:
        DoodleSceneImporter() = delete;

        /**
         * 解析场景 JSON
         *
         * @param bytes 完整 JSON 文件内容
         * @param sourceName 只用于错误消息，通常是文件路径
         * @return 已解析的场景数据；组件参数由各组件自己的 Deserialize 再校验
         * @throw std::runtime_error 语法错误、字段缺失、类型不对、未知字段、非有限数
         */
        [[nodiscard]] static DoodleSceneData Import(std::span<const std::byte> bytes,
                                                    std::string_view sourceName);
    };
}
