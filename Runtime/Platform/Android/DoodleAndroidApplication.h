//
//  DoodleAndroidApplication.h
//  Doodle
//
//  Created by 郭智 on 2026/10/10.
//

#pragma once

#include <cstdint>

#include "../DoodleApplication.h"

//前向声明。必须放在全局作用域。
//头文件里不引入 <android_native_app_glue.h>：本类的使用者（入口函数）
//本来就要 include 它，但没必要经本头文件带进来
struct android_app;

namespace Doodle
{
    /**
     * Android 的驱动层实现：窗口会消失又回来
     *
     * 覆盖了基类的三个生命周期函数，因为基类那套默认实现的前提（窗口与程序同寿）
     * 在这里不成立：
     *   Initialize —— Android 上「有窗口对象」和「有 ANativeWindow」是两件事。
     *                 前者构造窗口、接管 APP_CMD 即可；后者要等系统把窗口送过来，
     *                 再由 APP_CMD_INIT_WINDOW 驱动 Vulkan 起来
     *   Run        —— 循环多一个「现在能不能画」的闸门：退到后台但窗口尚存的那段
     *                 里继续满速出帧是要挨系统杀的。关闭请求也不再是唯一的退出条件
     *   Destroy    —— Vulkan 可能压根没起来过（一次都没拿到窗口就退出了），
     *                 这时候不能去拆一个不存在的实例
     *
     * 剩下那半边照旧用基类的：Vulkan 子系统、GetInstance、尺寸通知。
     * Android 的生命周期不是另起一套，只是把 InitializeVulkan / DestroyVulkan
     * 这对 helper 按另一个时刻表调用
     *
     * 本类由 Runtime/Platform/Android/DoodleAndroidMain.cpp 构造并驱动
     */
    class DoodleAndroidApplication final : public DoodleApplication
    {
    public:
        /**
         * @param pApp native_app_glue 给的 android_app，只借不放，
         *             生命周期由系统保证（比本对象长）
         */
        explicit DoodleAndroidApplication(android_app* pApp);

        ~DoodleAndroidApplication() override = default;

        DoodleAndroidApplication(const DoodleAndroidApplication&) = delete;
        DoodleAndroidApplication(DoodleAndroidApplication&&) = delete;
        DoodleAndroidApplication& operator=(const DoodleAndroidApplication&) = delete;

    public:
        // ---- DoodleApplication ----

        /**
         * 建窗口对象并接管 APP_CMD，不碰 Vulkan
         *
         * 覆盖基类默认实现。此刻还没有 ANativeWindow，Vulkan 起不来 ——
         * 那一步在拿到 APP_CMD_INIT_WINDOW 时由 OnAppCommand 做
         *
         * @note 必须先调，且只调一次：窗口在构造里接管 pApp->onAppCmd，
         *       不先建出来的话，之后的命令根本进不到本类
         */
        void Initialize() override;

        /**
         * 一直画到系统要求销毁 Activity
         *
         * 覆盖基类默认实现：多一条「窗口在且在前台」的闸门，
         * 退出条件也多一个「出过错，且不可能自行恢复」
         *
         * 循环里抛出的异常在本函数内消化掉并记进失败标志，不往外抛 ——
         * 入口函数据此走收尾流程（见 DoodleAndroidMain.cpp）
         */
        void Run() override;

        /**
         * 拆掉 Vulkan（如果起来过），再放掉窗口
         *
         * 覆盖基类默认实现：Vulkan 可能一次都没起来过，
         * 那时不能去拆一个不存在的实例
         */
        void Destroy() override;

        /**
         * 处理一条 APP_CMD_*，由 DoodleAndroidWindow 转交过来
         *
         * 窗口状态（是否前台）已经由那边消化掉了，到这里的是需要动
         * Vulkan 子系统的那些
         */
        void OnAppCommand(int32_t command);

        /**
         * 出过错，且不可能自行恢复
         *
         * 入口函数在收尾时据此请系统把本 Activity 关掉，好让用户看到
         * 「应用退出了」而不是黑屏挂着
         */
        [[nodiscard]] bool HasFailed() const;

    protected:
        /**
         * 建出 Android 窗口，并把驱动层自身交给它
         *
         * 窗口拿到本对象的引用之后，APP_CMD_* 就能直接送达 OnAppCommand
         */
        std::unique_ptr<DoodleWindow> CreateWindow() override;

    private:
        /** 胶水层的应用对象，只借不放 */
        android_app* m_pApp = nullptr;

        /** Vulkan 是否已经起来过。第一轮 INIT_WINDOW 是 Initialize，之后都是 Resume */
        bool m_initialized = false;

        /** 出过错，且不可能自行恢复。帧循环据此退出 */
        bool m_failed = false;
    };
}
