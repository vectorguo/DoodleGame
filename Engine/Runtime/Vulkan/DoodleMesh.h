//
//  DoodleMesh.h
//  Doodle
//
//  Created by 郭智 on 2026/10/24.
//

#pragma once

#include <string>
#include <string_view>
#include <vector>

#include <vulkan/vulkan_core.h>

#include "DoodleGraphicsBuffer.h"
#include "DoodleVertex.h"
#include "Rendering/DoodleSubmesh.h"

namespace Doodle
{
    /**
     * 一份网格资产：顶点缓冲 + 索引缓冲 + 子网格区间
     *
     * 几何数据住在设备本地显存里，本类只负责这组资源的生死。它不知道
     * GameObject、Transform、材质或绘制命令 —— 谁摆在哪里、画成什么颜色，
     * 是场景与渲染收集的事。
     *
     * 子网格不另建缓冲：整份网格仍只有一对顶点/索引缓冲，DoodleSubmesh 只是
     * 索引缓冲里的连续区间加上材质槽。绘制端按区间分段调用 vkCmdDrawIndexed，
     * 换材质不必拆资源，也不必每次把整份网格重新提交。区间由导入器按材质切好，
     * Initialize 会校验它们有序、连续、覆盖整个索引缓冲。
     *
     * 网格不保存材质数据：submesh.materialIndex 只是槽号，真正的 DoodleMaterial
     * 由 DoodleMeshRenderer 持有并按实例序列化。同一份网格可以被多个对象共用、
     * 各自配不同材质，资产缓存不会和外观绑死。
     *
     * 资产路径：文件资产在构造时传入（当前是相对文件路径），loader 的缓存仍以
     * 同一个字符串为键；运行时动态创建的实例走默认构造，路径为空，这种网格
     * 不能序列化。同一份网格被多个对象共享时路径只存一份，引用方通过
     * GetAssetPath() 取得。以后引入资产基类时，这个字段上移到基类。
     *
     * 不可复制、不可移动：两个 DoodleMesh 对象会各自销毁同一对 VkBuffer。
     * 使用方拿到的是 const 指针（非拥有）：文件资产的寿命由 loader 保证，
     * 运行时动态创建的实例由创建方保证；两者都要在 GPU 空闲时显式 Destroy。
     * loader 内部则用 unique_ptr 持有，不存在需要搬运对象本身的场景
     *
     * 索引类型固定为 uint32_t：FBX 拍平后顶点数很容易越过 65535，
     * 与 DoodleCommandBuffer::Record 里的 VK_INDEX_TYPE_UINT32 是同一个决定。
     * 不按模型大小分两套索引类型
     */
    class DoodleMesh
    {
    public:
        /**
         * 运行时动态创建：没有资产路径，不能被序列化
         */
        DoodleMesh() = default;

        /**
         * 文件资产：路径就是加载身份
         *
         * @param assetPath 资产的加载路径（当前是相对文件路径），必须非空
         */
        explicit DoodleMesh(std::string_view assetPath);

        DoodleMesh(const DoodleMesh&) = delete;
        DoodleMesh& operator=(const DoodleMesh&) = delete;
        DoodleMesh(DoodleMesh&&) = delete;
        DoodleMesh& operator=(DoodleMesh&&) = delete;

        /**
         * 把两份数据与子网格区间送上设备
         *
         * @param device 已初始化的 Vulkan 设备，且命令缓冲层的传输池已经建好
         * @param vertices 顶点数据，按 Vertex 的布局排列
         * @param indices 索引数据，每个值都必须小于 vertices.size()
         * @param submeshes 按材质切好的索引区间；必须有序、首尾相接、覆盖整个
         *        indices；materialIndex 从 0 开始连续分配
         *
         * @throw std::runtime_error 子网格区间不满足上面那组不变量。
         *        校验发生在任何 GPU 资源创建之前 —— 本类析构不兜底，
         *        不能在上传一半时抛出而留下无主的缓冲
         *
         * 收 const vector& 而不是 span：当前唯一调用方（DoodleMeshLoader）传的
         * 就是 DoodleFbxMeshData 的 vector，视图类型没有实际收益；出现第二种
         * 容器 / 子区间时再泛化
         *
         * @note 内部会提交一次拷贝并等待完成，只能在初始化期调用，
         *       不得与在途的帧并发 —— 见 DoodleGraphicsBuffer::CopyTo
         */
        void Initialize(const DoodleVulkanDevice& device,
                        const std::vector<Vertex>& vertices,
                        const std::vector<uint32_t>& indices,
                        const std::vector<DoodleSubmesh>& submeshes);

        /**
         * 销毁两份缓冲
         *
         * @note 前置条件：GPU 已空闲。文件资产由 loader 的 DestroyAsset 统一调用；
         *       运行时动态创建的实例由创建方调用
         */
        void Destroy();

        /**
         * 资产的加载路径（当前就是 DoodleAssetLoader 的缓存键）
         *
         * 文件资产在构造时确定、之后不变；空串表示运行时动态创建，这种网格
         * 不能序列化。它不参与渲染，供序列化与调试使用
         */
        [[nodiscard]] const std::string& GetAssetPath() const
        {
            return m_assetPath;
        }

        /**
         * 顶点缓冲句柄
         */
        [[nodiscard]] VkBuffer GetVertexBuffer() const
        {
            return m_vertexBuffer.GetBuffer();
        }

        /**
         * 索引缓冲句柄
         */
        [[nodiscard]] VkBuffer GetIndexBuffer() const
        {
            return m_indexBuffer.GetBuffer();
        }

        /**
         * 取第 submeshIndex 段子网格
         *
         * 返回网格持有的区间引用，不拷贝，寿命与网格相同。
         *
         * 下标必须落在 [0, GetSubmeshes().size())，本函数不做边界检查：
         * 调用方的下标来自收集侧对同一张表的遍历（DoodleDrawItem::submeshIndex），
         * 表在 Initialize 之后不可变，因此不会失效。给出越界下标是调用方的错误，
         * 不是可恢复状态。
         *
         * 与 GetSubmeshes() 的分工：绘制端一次只要一段，用这个避免把整张表
         * 暴露在调用点；需要遍历全部段（收集、材质槽统计）时再用另一个。
         */
        [[nodiscard]] const DoodleSubmesh& GetSubmesh(uint32_t submeshIndex) const
        {
            return m_submeshes[submeshIndex];
        }

        /**
         * 按材质切好的索引区间；绘制端逐段发 vkCmdDrawIndexed
         *
         * 区间有序、连续、覆盖整个索引缓冲，由 Initialize 校验
         */
        [[nodiscard]] const std::vector<DoodleSubmesh>& GetSubmeshes() const
        {
            return m_submeshes;
        }

        /**
         * 材质槽数量：子网格里最大 materialIndex + 1
         *
         * 导入器从 0 开始连续分配槽号，所以这个值就是 renderer 需要创建的
         * DoodleMaterial 个数。每次现算（子网格只有几段），不另存一份可能与
         * 子网格表漂移的状态
         */
        [[nodiscard]] uint32_t GetMaterialSlotCount() const
        {
            uint32_t slotCount = 0;
            for (const DoodleSubmesh& submesh : m_submeshes)
            {
                const uint32_t slot = submesh.materialIndex + 1;
                if (slot > slotCount)
                {
                    slotCount = slot;
                }
            }
            return slotCount;
        }

    private:
        /**
         * 资产的加载路径，文件资产在构造时确定；空串表示运行时动态创建。
         * 以后引入资产基类时上移到基类
         */
        std::string m_assetPath;

        /**
         * 顶点缓冲，走设备本地显存
         */
        DoodleGraphicsBuffer m_vertexBuffer;

        /**
         * 索引缓冲，走设备本地显存
         */
        DoodleGraphicsBuffer m_indexBuffer;

        /**
         * 子网格区间，CPU 侧数据；索引下标指向 m_indexBuffer，
         * materialIndex 由 renderer 解释
         */
        std::vector<DoodleSubmesh> m_submeshes;
    };
}
