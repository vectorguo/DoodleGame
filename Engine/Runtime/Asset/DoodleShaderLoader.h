//
//  DoodleShaderLoader.h
//  Doodle
//
//  Created by 郭智 on 2026/11/6.
//

#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "DoodleAssetLoader.h"
#include "Rendering/DoodleRenderState.h"
#include "Rendering/DoodleShader.h"
#include "Rendering/DoodleVertex.h"

namespace Doodle
{
    class DoodleComponentConfig;
    class DoodleFileSystem;
    class DoodleVulkanDevice;

    /**
     * 着色器加载器：Shaders/ 下的 *.shader.json → 每个 pass 一对 SPIR-V → DoodleShader
     *
     * 描述文件按 pass 分节，每节给出该 pass 的顶点 / 片元字节码路径与可选的
     * renderState；顶层给顶点格式与实例格式（网格数据与 pass 无关）。
     * 字节码本身由 SPIRV-Reflect 反射，set 1 布局按反射结果建 —— 描述文件里不写绑定。
     * 缓存与去重由 DoodleAssetLoader 基类按 key 负责，同一份描述被多个材质
     * 引用时只加载一次。
     *
     * 文件读取先走 DoodleFileSystem（桌面：运行时目录或 .app 包内），读不到再退到
     * 编进二进制的副本 —— Android 上 APK 里没有 Shaders/ 目录，只有内嵌表那一份。
     *
     * 线程约定：会创建 Vulkan 资源，只能在主线程、初始化期调用。
     */
    class DoodleShaderLoader final : public DoodleAssetLoader<DoodleShader>
    {
    public:
        DoodleShaderLoader(DoodleVulkanDevice& device, const DoodleFileSystem& fileSystem);

        DoodleShaderLoader(const DoodleShaderLoader&) = delete;
        DoodleShaderLoader& operator=(const DoodleShaderLoader&) = delete;

    protected:
        /**
         * 读描述文件、逐 pass 读字节码，反射并建出着色器资产
         *
         * @throws std::runtime_error 描述文件解析失败、passes 为空、pass 名不认识、
         *         字节码读取或反射失败，或违反某个 pass 的接口约定
         */
        [[nodiscard]] std::unique_ptr<DoodleShader> Create(std::string_view key) override;

        /**
         * 销毁着色器的 set 1 布局
         *
         * 前置条件：GPU 已空闲，且已经用不到这个着色器去建管线布局
         */
        void DestroyAsset(DoodleShader& shader) override;

    private:
        /**
         * 读一个着色器文件：先走文件来源，取不到再退到编进二进制的副本
         *
         * 文件来源在桌面上命中运行时目录里那一份；Android 的 APK assets 里没有
         * Shaders/，必然落到内嵌表
         *
         * @throws std::runtime_error 两个来源都取不到
         */
        [[nodiscard]] static std::vector<std::byte> ReadShaderFile(const DoodleFileSystem& fileSystem,
                                                                   const std::string& path);

        /**
         * 从描述文件的 renderState 对象读出渲染状态；缺省字段取 DoodleRenderState 的默认值
         *
         * @throws std::runtime_error 字段类型不对，或枚举字符串不认识
         */
        [[nodiscard]] static DoodleRenderState ReadRenderState(const DoodleComponentConfig& config);

        /**
         * pass 名的字符串 → 枚举
         *
         * 不认识的名字无条件报错（不只在 Debug）：拼错 pass 名会导致该 pass
         * 的描述凭空消失，必须当场说出来
         */
        [[nodiscard]] static EDoodlePassType ParseShaderPass(const std::string& value);

        /**
         * 顶点格式的字符串名 → 枚举
         */
        [[nodiscard]] static EVertexFormat ParseVertexFormat(const std::string& value);

        /**
         * 实例格式的字符串名 → 枚举
         */
        [[nodiscard]] static EInstanceFormat ParseInstanceFormat(const std::string& value);

        /**
         * 剔除模式的字符串名 → 枚举
         */
        [[nodiscard]] static ECullMode ParseCullMode(const std::string& value);

        /**
         * 深度比较方式的字符串名 → 枚举
         */
        [[nodiscard]] static ECompareOp ParseCompareOp(const std::string& value);

        /**
         * 混合方式的字符串名 → 枚举
         */
        [[nodiscard]] static EBlendMode ParseBlendMode(const std::string& value);

        /**
         * 图元拓扑的字符串名 → 枚举
         */
        [[nodiscard]] static EPrimitiveTopology ParseTopology(const std::string& value);

    private:
        /**
         * Vulkan 设备，非拥有；寿命由 DoodleApplication 保证
         */
        DoodleVulkanDevice* m_pDevice = nullptr;
    };
}
