//
//  DoodleMeshRenderer.h
//  Doodle
//
//  Created by 郭智 on 2026/10/25.
//

#pragma once

#include <string>

#include "Core/DoodleComponent.h"

namespace Doodle
{
    //只以指针出现，完整定义留给使用它的源文件
    class DoodleMesh;

    /**
     * 网格渲染器：告诉渲染收集「这个对象用哪份网格画」
     *
     * 只保存一份网格资产的非拥有指针，不持有顶点 / 索引缓冲，也不知道
     * Vulkan。网格本身由资产管理器内部的 DoodleMeshLoader 持有；
     * 销毁顺序（场景 → 资产管理器 → Vulkan）保证渲染器活着时指针一直有效。
     *
     * 空指针表示不画：对象还没有网格、或网格已经被摘掉是合法状态，
     * 收集侧跳过它，不是错误。
     *
     * 变换不在这里：位置 / 旋转 / 缩放来自宿主的 DoodleTransform，
     * 收集时现取 GetMatrix()，本类不缓存矩阵，也就不会出现「改了
     * Transform、渲染器还拿着旧矩阵」这种状态。
     */
    class DoodleMeshRenderer final : public DoodleComponent
    {
    public:
        /**
         * 把 mesh key 写回序列化配置
         *
         * 只写 Deserialize 保存下来的 key；运行时通过 SetMesh 替换的网格
         * 没有对应的内容 key，不会更新这里。
         */
        void Serialize(DoodleComponentConfig& config) const override;

        /**
         * 从序列化配置读取 mesh key，并向资产管理器取网格
         *
         * key 会保存下来供 Serialize 使用；取不到资产时抛异常。
         * 这个方法只做字段/资产解析，不依赖宿主，所以 Debug 校验探针也能调用。
         */
        void Deserialize(const DoodleComponentConfig& config) override;

        /**
         * 设置要画的网格；传 nullptr 表示暂时不画
         *
         * 指针非拥有，寿命必须由网格库覆盖到本组件销毁为止。
         * 这个入口不更新 mesh key —— 反序列化出来的组件才带可写回的 key，
         * 运行时直接替换网格属于运行时状态，不当作场景内容序列化。
         */
        void SetMesh(const DoodleMesh* pMesh)
        {
            m_pMesh = pMesh;
        }

        /**
         * 当前网格，可能为空
         */
        [[nodiscard]] const DoodleMesh* GetMesh() const
        {
            return m_pMesh;
        }

        /**
         * 反序列化时保存的 mesh key；运行时添加的组件为空
         */
        [[nodiscard]] const std::string& GetMeshKey() const
        {
            return m_meshKey;
        }

    private:
        /**
         * 网格资产，非拥有；寿命由加载它的 loader 保证（资产管理器 Destroy 之前）
         */
        const DoodleMesh* m_pMesh = nullptr;

        /**
         * 场景里引用的 mesh key，Serialize 用
         */
        std::string m_meshKey;
    };
}
