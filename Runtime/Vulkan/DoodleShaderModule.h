//
// Created by 郭智 on 2026/10/5.
//

#pragma once

#include <string>
#include <vector>
#include <vulkan/vulkan_core.h>

namespace Doodle
{
    /**
     * 着色器模块工具
     *
     * 纯静态类，不可实例化。这里刻意是「工具」而不是「一层」——
     * 无状态、不持有任何句柄、没有 Initialize / Destroy，
     * 也不挂到 DoodleVulkanManager 上。
     *
     * 理由：VkShaderModule 只是待编译字节码的薄包装，真正的编译发生在
     * vkCreateGraphicsPipelines。管线不持有模块，只持有编译结果，所以
     * 模块的生命周期天然就是一次函数调用——用局部变量管住即可。
     * 把它做成层会把寿命从「一次管线创建」拉长到「整个程序」，只会
     * 凭空多出销毁顺序、交换链重建时要不要重建之类的异常路径。
     *
     * 将来若出现「多条管线共享同一份模块」，宿主应该是 DoodleGraphicsPipeline
     * 那一层，仍然不是独立的一层。
     */
    class DoodleShaderModule
    {
    public:
        // 纯静态工具类，不允许实例化
        DoodleShaderModule() = delete;

        /**
         * 从 SPIR-V 文件创建着色器模块
         *
         * 路径解析顺序（取第一个能打开的）：
         *   1. shaderPath 本身 —— 相对「进程当前工作目录」。开发时走这条，
         *      需把工作目录设为工程根目录（CLion 的 Run Configuration）
         *   2. <可执行文件目录>/../Resources/shaderPath —— macOS .app 包内。
         *      从 Finder 双击启动时工作目录是 /，走这条
         *
         * 调用方须在管线创建完成后调用 vkDestroyShaderModule 销毁。
         *
         * @param pDevice 逻辑设备
         * @param shaderPath SPIR-V 文件路径，如 "Shaders/vert.spv"
         * @return 着色器模块
         * @throw std::runtime_error 文件打不开、或模块创建失败时抛出
         */
        [[nodiscard]] static VkShaderModule CreateShaderModule(VkDevice pDevice, const std::string& shaderPath);

    private:
        /**
         * 取可执行文件所在目录的绝对路径；取不到返回空串
         *
         * 用来在 .app 包内定位资源：可执行文件在 Contents/MacOS/，
         * 资源在 Contents/Resources/，两者的相对位置是固定的
         */
        [[nodiscard]] static std::string GetExecutableDirectory();

        /**
         * 读取整个 SPIR-V 文件
         *
         * 按候选顺序逐个尝试，取第一个能打开的：
         *   1. filename 本身 —— 相对「进程当前工作目录」
         *   2. <可执行文件目录>/../Resources/filename —— macOS .app 包内
         *
         * @param filename 文件路径
         * @return 文件的全部字节
         * @throw std::runtime_error 所有候选都打不开时抛出，消息里列出尝试过的路径
         */
        [[nodiscard]] static std::vector<char> ReadFile(const std::string& filename);
    };
}
