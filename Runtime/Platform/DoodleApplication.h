//
//  DoodleApplication.h
//  Doodle
//
//  Created by 郭智 on 2026/9/1.
//
#pragma once

#include <memory>

#include "DoodleWindow.h"
#include "Vulkan/DoodleVulkanManager.h"

namespace Doodle
{
    /**
     * 应用对象：驱动帧循环、按顺序起停 Vulkan 子系统
     *
     * 两个平台共用这一个「当前应用」的概念，所以它得同时是两件事：
     * 平台无关的那半边（持有 Vulkan 子系统、接收窗口的尺寸通知、被 C 回调
     * 经 GetInstance 找到），以及一套现成的生命周期默认实现。
     *
     * 「窗口从哪来」不在本类 —— 具体建什么窗、怎么建，由平台子类在
     * CreateWindow 里回答（macOS 那份见 Runtime/Platform/MacOS/DoodleMacApplication.h）
     *
     * Initialize / Run / Destroy 三个虚函数带一套默认实现，适用条件是
     * **窗口与程序同寿**：循环每次都是「泵事件 → 画一帧」，不需要判断窗口还在不在，
     * 也没有 Suspend / Resume。
     *
     * 不满足这条的平台覆盖它们 —— 目前是 Android：那边的窗口会消失又回来，
     * 事件泵还必须自己做阻塞 / 非阻塞的取舍（见 DoodleAndroidWindow::PumpEvents），
     * 所以它的三个生命周期函数在
     * Runtime/Platform/Android/DoodleAndroidApplication.h 里另有实现。
     *
     * 覆盖时只有一条硬要求：Vulkan 各层的起停顺序是 InitializeVulkan /
     * DestroyVulkan 这对 helper 的事，别绕开它们自己拼。除此之外时刻表随平台定，
     * 这正是「窗口与程序同寿」这条前提失效时唯一需要重写的东西
     */
    class DoodleApplication
    {
    public:
        virtual ~DoodleApplication();

        DoodleApplication(const DoodleApplication&) = delete;
        DoodleApplication(DoodleApplication&&) = delete;
        DoodleApplication& operator= (const DoodleApplication&) = delete;

    public:
        /**
         * 当前应用实例
         *
         * 存在的理由：GLFW 与胶水层的回调都是 C 函数指针，没有 this 可用。
         * 有了这个出口，窗口实现不必再让构造函数收一个回调进来 ——
         * 「变了之后告诉谁」由窗口自己在这里问，接不接受得了是驱动层的事
         *
         * 有效期就是应用对象的存活期：构造时登记、析构时清空，都是本类自己做的。
         * 进程里没有应用对象时（构造之前、析构之后）取用是未定义行为
         *
         * 返回引用而不是指针：那个空状态没有任何调用者够得着 —— 窗口只可能
         * 在 Initialize 里建出来，而回调只在泵事件时触发，那一刻应用对象必然活着。
         * 把空状态写进签名，只会让每个调用点配一句永远为真的判断，看着像防御，
         * 其实是在替一个不存在的状态操心，还顺手把「这里可能没有实例」
         * 这个错误印象留给了下一个人
         *
         * @note 存活期内不会变：本工程同一进程只有一个应用对象
         */
        [[nodiscard]] static DoodleApplication& GetInstance()
        {
            return *s_pInstance;
        }

        /**
         * 建窗口，然后按顺序起 Vulkan 各层
         *
         * 默认实现，前提是窗口与程序同寿。顺序不能反：表面建在窗口上，
         * 窗口得先有
         *
         * 窗口一出世就紧接着起 Vulkan，中间没有留给事件循环的缝 ——
         * 尺寸变化回调因此不可能赶在交换链建好之前置位「需要重建」，
         * 第一帧那次白重建从「靠注释提醒别踩」变成了「没机会踩」。
         * 这条保证只属于本默认实现：覆盖它的平台（Android）里窗口先于 Vulkan
         * 单独存在，那条缝是存在的，得自己处理
         *
         * @note 必须先调，且只调一次：Run 与 Destroy 都假设窗口已经在了
         */
        virtual void Initialize();

        /**
         * 一直画到窗口关闭
         *
         * 默认实现：泵事件 → 画一帧，循环条件只认关闭请求
         *
         * @note 必须先调 Initialize
         */
        virtual void Run();

        /**
         * 按依赖倒序销毁：先 Vulkan 后窗口
         *
         * 交换链、表面都依附于窗口，窗口先没的话，销毁它们就是拿着一个
         * 已经作废的句柄去用
         *
         * @note 必须先调 Initialize
         */
        virtual void Destroy();

        /**
         * 窗口尺寸变了，下一帧重建交换链
         *
         * 由平台的窗口实现直接调用（macOS 见 DoodleMacWindow::FramebufferSizeCallback），
         * 本对象不自己监听窗口事件。
         * 只记标志、不在回调里当场重建，理由见 DoodleVulkanManager::NotifyWindowResized
         */
        void NotifyWindowResized();

    protected:
        /**
         * 构造即登记为当前实例
         *
         * 登记放在这里而不是 Initialize 里：登记的时机本该是「这个对象存在了」，
         * 而不是「这个对象开始干活了」。绑到构造函数上之后，实例的有效期
         * 与对象的存活期是同一条线，不用再去论证「哪些调用会泵事件、
         * 会不会赶在登记之前」——窗口是 Initialize 里才建的，比构造晚
         */
        DoodleApplication();

        /**
         * 建出本平台的窗口，所有权交出来
         *
         * 由 Initialize 调用（覆盖了 Initialize 的平台就由它自己那份调）。
         * 返回空指针视为实现出错，会在那里当场抛异常
         *
         * 声明成虚函数、而不是「让入口建好了再交进来」：窗口的参数（尺寸、标题）
         * 是各平台自己的事，本层既不该知道也不该转发。交进来的写法还得在
         * 本类外面约定谁负责销毁，返回 unique_ptr 则把建与销收在同一条链上
         */
        virtual std::unique_ptr<DoodleWindow> CreateWindow() = 0;

        /**
         * 起 Vulkan 各层
         *
         * 窗口以引用交出去：Vulkan 各层只借不放（见 DoodleVulkanManager::Initialize），
         * 持有者仍在本类
         *
         * 覆盖生命周期的平台也用这个，别自己按别的顺序拼：各层的起停顺序
         * 只有这里和 DestroyVulkan 知道
         */
        void InitializeVulkan();

        /**
         * 销毁 Vulkan 各层
         */
        void DestroyVulkan();

    protected:
        /**
         * 窗口
         *
         * 由 CreateWindow 交出来，本类接住并负责到底：Destroy 里在 Vulkan 之后释放，
         * 而 Initialize 半路抛异常时，成员自己的析构也会兜住 —— 用 unique_ptr
         * 而不是裸指针就是为了后一条路。类型是接口而不是某个具体窗口 ——
         * 本类不该知道窗口是哪来的
         *
         * 平台那份实现需要窗口的具体类型时（Android 要 IsRenderable），
         * 自己 static_cast 取回 —— 所有权仍然只有这一处，别再加一个成员指向它
         */
        std::unique_ptr<DoodleWindow> m_pWindow;

        /**
         * Vulkan子系统
         */
        DoodleVulkanManager m_vulkanManager;

    private:
        /**
         * 当前实例，由构造置位、析构清空，成对维护
         *
         * 类型是基类：窗口实现只该看见「有个应用」，不该知道是哪一份。
         * 不设成保护的 —— 子类没有碰它的理由，置位与清空的时机只有构造和析构
         */
        static DoodleApplication* s_pInstance;
    };
}
