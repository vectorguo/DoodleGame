//
//  DoodleAsset.h
//  Doodle
//
//  Created by 郭智 on 2026/11/1.
//

#pragma once

#include <string>
#include <string_view>

namespace Doodle
{
    /**
     * 资产基类：被资产系统加载 / 引用的 GPU 内容共有的身份
     *
     * 放在 Asset：它只描述资产身份（路径），与 loader 同属资产层；
     * 具体 GPU 资源仍由 Rendering 下的资产类型持有。
     *
     * 目前只有一份数据：assetPath。它既是 loader 缓存里的键，也是序列化时
     * 写回的身份。运行时动态创建的资产路径为空，不能被序列化 —— 这条约定
     * 从 DoodleMesh / DoodleTexture 上移到这里，两个资产共用同一种口径。
     *
     * 不在这里放 Initialize / Destroy：两者的参数与资源种类由具体资产决定
     * （网格是顶点 / 索引缓冲，纹理是图像 / 采样器），基类只回答「我是谁」。
     *
     * 派生类不可复制 / 移动：它们各自持有 GPU 句柄，复制一份就会变成两个对象
     * 去销毁同一组句柄。约束放在基类，所有资产共用一条规则。
     *
     * 虚析构现在没有 delete-through-base 的调用者，但资产表将来若以
     * DoodleAsset* 统一持有，这条必须已经在。析构定义在 .cpp，避免每个
     * 翻译单元各生成一份 vtable
     */
    class DoodleAsset
    {
    public:
        /**
         * 运行时动态创建：没有资产路径，不能被序列化
         */
        DoodleAsset() = default;

        /**
         * 文件资产：路径就是加载身份
         *
         * @param assetPath 资产的加载路径（当前是相对文件路径），必须非空
         * @throws std::invalid_argument 路径为空
         */
        explicit DoodleAsset(std::string_view assetPath);

        /**
         * 虚析构
         */
        virtual ~DoodleAsset();

        //资产对象各自持有 GPU 句柄，复制 / 移动会变成两份去销毁同一组句柄
        DoodleAsset(const DoodleAsset&) = delete;
        DoodleAsset& operator=(const DoodleAsset&) = delete;
        DoodleAsset(DoodleAsset&&) = delete;
        DoodleAsset& operator=(DoodleAsset&&) = delete;

        /**
         * 资产的加载路径（当前就是 DoodleAssetLoader 的缓存键）
         *
         * 文件资产在构造时确定、之后不变；空串表示运行时动态创建，
         * 这种资产不能序列化。它不参与渲染，供序列化与调试使用
         */
        [[nodiscard]] const std::string& GetAssetPath() const
        {
            return m_assetPath;
        }

    private:
        /**
         * 资产的加载路径；空串表示运行时动态创建
         */
        std::string m_assetPath;
    };
}
