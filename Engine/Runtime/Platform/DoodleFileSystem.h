//
//  DoodleFileSystem.h
//  Doodle
//
//  Created by 郭智 on 2026/10/25.
//

#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace Doodle
{
    /**
     * 文件来源：把「字节从哪来」与「字节是什么格式」分开
     *
     * 资产加载只认这个接口，不再各自拼路径。桌面实现是工作目录 + .app 包内
     * Resources，Android 实现是 APK assets + 文件系统 —— 平台差异关在实现里，
     * 上层的场景 / 网格 / 纹理 loader 看到的都是同一个相对路径。
     *
     * 生命周期挂在 DoodleApplication 上，不跟着 Vulkan 设备走：Android 切到后台
     * 会拆掉表面相关的 Vulkan 层，但资产文件仍然可读。资产管理器只是持有它的引用，
     * 由应用保证它比资产管理器活得久。
     *
     * 线程约定：实现必须无状态，ReadBinary / Exists 是 const 且可被工作线程并发调用。
     * 现在所有加载都在主线程，这条约定是给将来异步 I/O 留的。
     */
    class DoodleFileSystem
    {
    public:
        DoodleFileSystem() = default;
        virtual ~DoodleFileSystem() = default;

        DoodleFileSystem(const DoodleFileSystem&) = delete;
        DoodleFileSystem& operator=(const DoodleFileSystem&) = delete;

        /**
         * 路径是否存在
         *
         * 只用于可选资产与诊断；常规加载路径直接读，让「读不到」在同一个地方
         * 报出尝试过的完整路径。
         */
        [[nodiscard]] virtual bool Exists(std::string_view path) const = 0;

        /**
         * 读取整个文件，按字节返回
         *
         * 路径的相对语义由实现决定，调用方只传资产路径（如 "Scenes/main.json"）。
         * 读不到时抛 std::runtime_error，消息里列出尝试过的位置。
         *
         * @note 空文件返回空 vector，不当成错误；格式层自己判断内容是否合法
         */
        [[nodiscard]] virtual std::vector<std::byte> ReadBinary(std::string_view path) const = 0;

        /**
         * 读取文本文件
         *
         * 非虚便利方法，内部走 ReadBinary：文本与二进制在文件系统这一层没有区别，
         * 不做换行转换、不做编码转换，也不额外补 '\0'。
         */
        [[nodiscard]] std::string ReadText(std::string_view path) const;
    };
}
