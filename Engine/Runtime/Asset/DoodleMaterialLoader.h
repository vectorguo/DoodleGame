//
//  DoodleMaterialLoader.h
//  Doodle
//
//  由 DoodleMaterial 资产化新增
//

#pragma once

#include <memory>
#include <string_view>

#include "DoodleAssetLoader.h"
#include "Rendering/DoodleMaterial.h"

namespace Doodle
{
    class DoodleFileSystem;

    /**
     * 材质加载器：Materials/ 下的 JSON 文件 → DoodleMaterial
     *
     * 文件结构是一个 JSON object，两个字段都可省略：
     *   {
     *     "baseColor": [r, g, b, a],
     *     "texture": "Art/SceneRes/Building/House1/House1_d.tga"
     *   }
     *
     * texture 是相对 Resources 根的资产路径，与场景文件同一口径；空串或省略
     * 表示不配纹理，绘制侧绑定默认 1×1 白图。baseColor 按 sRGB 书写。
     *
     * 缓存与去重由 DoodleAssetLoader 基类按 key 负责；材质不持有 GPU 资源，
     * 因此不需要重写 DestroyAsset。
     *
     * 线程约定：加载会递归加载纹理（创建 Vulkan 资源），只能在主线程、初始化期调用。
     */
    class DoodleMaterialLoader final : public DoodleAssetLoader<DoodleMaterial>
    {
    public:
        explicit DoodleMaterialLoader(const DoodleFileSystem& fileSystem);

        DoodleMaterialLoader(const DoodleMaterialLoader&) = delete;
        DoodleMaterialLoader& operator=(const DoodleMaterialLoader&) = delete;

    protected:
        /**
         * 读取并解析一份材质文件，再递归加载它引用的纹理
         *
         * @throws std::runtime_error 文件读取 / JSON 解析失败、字段类型不对，
         *         或引用的纹理加载失败
         */
        [[nodiscard]] std::unique_ptr<DoodleMaterial> Create(std::string_view key) override;
    };
}
