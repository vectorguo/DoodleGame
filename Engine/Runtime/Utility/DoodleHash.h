//
//  DoodleHash.h
//  Doodle
//
//  Created by 郭智 on 2026/11/6.
//

#pragma once

#include <cstddef>
#include <functional>
#include <string_view>
#include <vector>

namespace Doodle
{
    /**
     * 拼组合哈希的工具
     *
     * 给「按内容做键」的缓存用：键由几段内容拼成（字节码、渲染状态……），
     * 每段先各自算哈希，再按顺序混进同一个种子。哈希只负责分桶 ——
     * 哈希相同不代表内容相同，命中之后调用方必须再逐项比对
     *
     * 全是静态方法，理由与 DoodleVulkanUtils 相同：工具没有生命周期
     */
    class DoodleHash final
    {
    public:
        DoodleHash() = delete;

        /**
         * 把 value 混进 seed
         *
         * boost::hash_combine 的写法：常数打散低位，移位让顺序参与结果 ——
         * 先混 a 再混 b，与先混 b 再混 a 得到的值不同
         */
        static void Combine(std::size_t& seed, const std::size_t value)
        {
            seed ^= value + kGoldenRatio + (seed << 6) + (seed >> 2);
        }

        /**
         * 一段字节的哈希值
         *
         * 借 std::hash<std::string_view>：标准库给字符串用的就是成熟的字节哈希，
         * 不必自己再写一份。string_view 只是 {指针, 长度} 的视图，不拷贝字节；
         * 这层转换也不是多余的 —— string_view 没有成员哈希函数，标准库里能用的
         * 只有 std::hash<std::string_view> 这一个入口。
         * 空数组的 data() 可能为空指针，(nullptr, 0) 仍是合法的空区间
         */
        [[nodiscard]] static std::size_t BytesToHashValue(const std::vector<std::byte>& bytes)
        {
            const std::string_view view(reinterpret_cast<const char*>(bytes.data()), bytes.size());
            return std::hash<std::string_view>{}(view);
        }

        /**
         * 浮点的哈希值
         *
         * 单独开这个入口只为一件事：std::hash<float> 把 +0 与 -0 归成同一个值，
         * 与 == 一致。整数 / 枚举没有这个问题，调用方直接 static_cast 后进 Combine
         */
        [[nodiscard]] static std::size_t FloatToHashValue(const float value) noexcept
        {
            return std::hash<float>{}(value);
        }

    private:
        /**
         * 黄金分割比的定点表示，按 size_t 的宽度取 64 位或 32 位那一份
         */
        static constexpr std::size_t kGoldenRatio = sizeof(std::size_t) >= 8
                                                        ? static_cast<std::size_t>(0x9e3779b97f4a7c15ull)
                                                        : static_cast<std::size_t>(0x9e3779b9u);
    };
}
