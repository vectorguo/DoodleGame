//
//  DoodleAndroidFileSystem.h
//  Doodle
//
//  Created by 郭智 on 2026/10/25.
//

#pragma once

#include "../DoodleFileSystem.h"

//前向声明。必须放在全局作用域。
//头文件里不引入 <android/asset_manager.h>：本类的使用者只该看见文件来源本身，
//AAssetManager 只在实现文件里用
struct AAssetManager;

namespace Doodle
{
    /**
     * Android 文件来源：APK assets + 文件系统
     *
     * 两处来源，按顺序尝试：
     *   1. APK assets（AAssetManager）—— 随 APK 打包的 Resources/。构建时由
     *      android/app/build.gradle.kts 把仓库根的 Resources/ 整个收作 assets
     *      源目录，所以路径与桌面完全一致（如 "Scenes/main.json"）
     *   2. 文件系统 —— 给 adb push 进设备的外部文件留的入口。相对路径按进程
     *      工作目录解释（Android 上通常是 "/"），实际用得上的是绝对路径
     *
     * AAssetManager 由 NativeActivity 提供，进程存活期间一直有效；本类只借不放。
     * 接口与桌面实现相同，上层 loader 不需要知道资产来自 APK 还是磁盘
     *
     * 线程约定沿用基类：本类没有可变状态，ReadBinary / Exists 可以被工作线程
     * 并发调用。AAssetManager 的打开与读取本身是线程安全的
     */
    class DoodleAndroidFileSystem final : public DoodleFileSystem
    {
    public:
        /**
         * @param pAssetManager NativeActivity 的资产管理器，来自
         *                      android_app::activity->assetManager，不能为空
         */
        explicit DoodleAndroidFileSystem(AAssetManager* pAssetManager);
        ~DoodleAndroidFileSystem() override = default;

        [[nodiscard]] bool Exists(std::string_view path) const override;

        [[nodiscard]] std::vector<std::byte> ReadBinary(std::string_view path) const override;

    private:
        /** 只借不放：生命周期由 NativeActivity 覆盖整个进程 */
        AAssetManager* m_pAssetManager = nullptr;
    };
}
