# 最低支持 CMake 3.22。
#
# 这份文件里没有任何 4.x 专有语法，用得最新的三处是：
#   find_library(... REQUIRED)    （3.18）
#   list(PREPEND)                 （3.15）
#   CMAKE_CXX_STANDARD 23 这个取值（3.20）
# 原先写 4.3（CLion 新建工程时按自带版本号填的），结果 Android SDK 自带的
# CMake 最高只到 4.1.2，两条 Android 路径全被挡住，反而只能依赖 CLion。
#
# 降到 3.22 之后几条链各有各的 CMake，互不干扰：
#   Android：SDK 的 3.22.1 + NDK clang，C++23 展开成 -std=gnu++2b（与 gnu++23 同义）
#   macOS  ：CLion 自带的 4.3.1
#   Windows：Visual Studio 2022 自带的那份（实测 3.31），MSVC 上 C++23 展开成 /std:c++latest。
#            用 windows/generate.bat 生成 sln，它会优先挑这一份
# 别拿 3.22.1 去建 macOS target —— 它的 AppleClang 表只到 C++20，
# 会把 C++23 报成「CMake does not know the compile flags」。
#
# 另外这一行不只是「声明最低版本」：它把该版本及之前的策略置为 NEW、
# 之后的保持 OLD，动这个数字等于动了 CMake 的策略基线。
cmake_minimum_required(VERSION 3.22)

# 工程名 / 主 target 名。桌面是 add_executable(DoodleGame ...)，Android 是
# add_library(DoodleGame SHARED ...)，名字必须和这里一致。
#
# 不带 LANGUAGES 参数 = 启用默认语言，即 C 和 CXX。这个默认值不能少 ——
# Android 侧要把 NDK 的 native_app_glue.c 编进来，那是个 .c 文件：
# 一旦 C 没启用，CMake 会用 clang++ 去编它，ANativeActivity_onCreate 会被
# C++ 名字修饰改名，框架按名字 dlsym 找不到入口，
# 表现为 APK 装得上、点不开、没有任何报错
project(DoodleGame)

# STANDARD 23          = 用 C++23
# STANDARD_REQUIRED ON = 编译器不支持就报错，不悄悄降级
# EXTENSIONS ON        = 允许 GNU 扩展（gnu++23，而不是纯 c++23）
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

# GLM 的投影函数默认按 OpenGL 的裁剪空间生成深度（[-1, 1]），
# Vulkan 的 NDC 深度范围是 [0, 1]。这个宏必须对所有 GLM 使用者保持一致，
# 所以放在编译命令行上，而不是某个 .cpp 里单独 #define ——
# 后者会让不同翻译单元看到不同的 GLM 配置，属于 ODR 问题。
add_compile_definitions(GLM_FORCE_DEPTH_ZERO_TO_ONE)

# 最低支持的 macOS 版本。只在 Mac 上生效。
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "15.5")
endif()

# ============================================================================
# 源文件清单
#
# 按「谁能编」和「属于哪一层」分几摞。分开的理由是实的：GLFW 实现里全是
# #include <GLFW/glfw3.h>，Android 侧根本没有这个库；反过来 Android 入口依赖
# <android_native_app_glue.h>，桌面也没有；Windows 那摞依赖 <windows.h>，另外两边都没有。
# 混在一摞里只能靠 #ifdef 挡，那是把平台判断散到各处，不如在这里一次说清
#
# 平台专属的那三摞，路径与 Engine/Runtime/Platform/<平台>/ 一一对应：
# 一个平台的窗口实现、文件来源和 Application 都在自己那个文件夹里。
# 平台入口（main / android_main）不在 Engine —— 它们属于 App 组合根，
# 单独列在下面的 DOODLE_APP_*_SOURCES 里。
# Platform/ 根目录下的是各摞共用的那部分（接口、驱动层、日志）
#
# DoodleApplication 编进了共享那摞而不是 macOS 那摞：它不依赖任何平台 API。
# 各个平台都要用它 —— 它是「当前应用」这个概念本身（持有 Vulkan 子系统、
# 接收窗口的尺寸通知、被 C 回调经 GetInstance 找到），只是三个生命周期函数
# 带的那套默认实现以「窗口与程序同寿」为前提，所以 Android 另有一份覆盖它们的
# DoodleAndroidApplication（在 Android 那摞里）
# ============================================================================

# 与平台无关的部分：Vulkan 各层 + 窗口抽象 + 日志
set(DOODLE_SHARED_SOURCES
    Engine/Runtime/Vulkan/DoodleVulkanDevice.cpp
    Engine/Runtime/Vulkan/DoodleVulkanDevice.h
    Engine/Runtime/Vulkan/DoodleSwapChain.cpp
    Engine/Runtime/Vulkan/DoodleSwapChain.h
    Engine/Runtime/Vulkan/DoodleGraphicsPipeline.cpp
    Engine/Runtime/Vulkan/DoodleGraphicsPipeline.h
        Engine/Runtime/Vulkan/DoodleCommandBuffer.cpp
    Engine/Runtime/Vulkan/DoodleCommandBuffer.h
    Engine/Runtime/Vulkan/DoodleSyncObjects.cpp
    Engine/Runtime/Vulkan/DoodleSyncObjects.h
    Engine/Runtime/Vulkan/DoodleGraphicsBuffer.cpp
    Engine/Runtime/Vulkan/DoodleGraphicsBuffer.h
    Engine/Runtime/Vulkan/DoodleMesh.cpp
    Engine/Runtime/Vulkan/DoodleMesh.h
    Engine/Runtime/Vulkan/DoodleDepthImages.cpp
    Engine/Runtime/Vulkan/DoodleDepthImages.h
    Engine/Runtime/Vulkan/DoodleImage.cpp
    Engine/Runtime/Vulkan/DoodleImage.h
    Engine/Runtime/Vulkan/DoodleTexture.cpp
    Engine/Runtime/Vulkan/DoodleTexture.h
    Engine/Runtime/Vulkan/DoodleVulkanManager.cpp
    Engine/Runtime/Vulkan/DoodleVulkanManager.h
    Engine/Runtime/Vulkan/DoodleShaderModule.cpp
    Engine/Runtime/Vulkan/DoodleShaderModule.h
    Engine/Runtime/Vulkan/DoodleVulkanConfig.h
    Engine/Runtime/Vulkan/DoodleVulkanUtils.cpp
    Engine/Runtime/Vulkan/DoodleVulkanUtils.h
    Engine/Runtime/Vulkan/DoodleVertex.h
    Engine/Runtime/Vulkan/QueueFamilySelection.h
    Engine/Runtime/Vulkan/SwapChainSupportDetails.h
    Engine/Runtime/Platform/DoodleWindow.h
    Engine/Runtime/Platform/DoodleInput.cpp
    Engine/Runtime/Platform/DoodleInput.h
    Engine/Runtime/Platform/DoodleFileSystem.cpp
    Engine/Runtime/Platform/DoodleFileSystem.h
    Engine/Runtime/Platform/DoodleApplication.cpp
    Engine/Runtime/Platform/DoodleApplication.h
    Engine/Runtime/Platform/DoodleLog.cpp
    Engine/Runtime/Platform/DoodleLog.h
    Engine/Runtime/Math/DoodleMath.cpp
    Engine/Runtime/Math/DoodleMath.h
    Engine/Runtime/Core/DoodleTransform.h
    Engine/Runtime/Core/DoodleTime.cpp
    Engine/Runtime/Core/DoodleTime.h
    Engine/Runtime/Core/DoodleLifecycleState.h
    Engine/Runtime/Core/DoodleComponent.h
    Engine/Runtime/Core/DoodleGameObject.h
    Engine/Runtime/Core/DoodleGameObject.cpp
    Engine/Runtime/Asset/DoodleAssetLoader.h
    Engine/Runtime/Asset/DoodleAssetManager.cpp
    Engine/Runtime/Asset/DoodleAssetManager.h
    Engine/Runtime/Asset/DoodleMeshLoader.cpp
    Engine/Runtime/Asset/DoodleMeshLoader.h
    Engine/Runtime/Asset/DoodleSceneLoader.cpp
    Engine/Runtime/Asset/DoodleSceneLoader.h
    Engine/Runtime/Asset/DoodleFbxImporter.cpp
    Engine/Runtime/Asset/DoodleFbxImporter.h
    Engine/Runtime/Asset/DoodleSceneImporter.cpp
    Engine/Runtime/Asset/DoodleSceneImporter.h
    Engine/ThirdParty/ufbx/ufbx.c
    Engine/ThirdParty/ufbx/ufbx.h
    Engine/Runtime/Scene/DoodleScene.h
    Engine/Runtime/Scene/DoodleScene.cpp
    Engine/Runtime/Core/DoodleComponentConfig.h
    Engine/Runtime/Core/DoodleComponentConfig.cpp
    Engine/Runtime/Core/DoodleComponentRegistry.h
    Engine/Runtime/Core/DoodleComponentRegistry.cpp
    Engine/Runtime/Scene/DoodleSceneData.h
    Engine/Runtime/Scene/DoodleSceneManager.h
    Engine/Runtime/Scene/DoodleSceneManager.cpp
    Engine/Runtime/Rendering/DoodleMeshRenderer.h
    Engine/Runtime/Rendering/DoodleMeshRenderer.cpp
    Engine/Runtime/Rendering/DoodleDrawItem.h
    Engine/Runtime/Rendering/DoodleShaderData.h
    Engine/Runtime/Rendering/DoodleSubmesh.h
    Engine/Runtime/Rendering/DoodleMaterial.h
    Engine/Runtime/Rendering/DoodleDrawContext.h
    Engine/Runtime/Rendering/DoodleCamera.cpp
    Engine/Runtime/Rendering/DoodleCamera.h
    Engine/Runtime/Rendering/DoodleLight.h
    Engine/Runtime/Rendering/DoodleLight.cpp
    Engine/Runtime/Core/DoodleBuiltinComponentRegistration.h
    Engine/Runtime/Core/DoodleBuiltinComponentRegistration.cpp
    Engine/Runtime/Utility/Singleton.h
)

# macOS 专属：窗口与文件来源实现
set(DOODLE_MACOS_SOURCES
        Engine/Runtime/Platform/MacOS/DoodleMacWindow.cpp
        Engine/Runtime/Platform/MacOS/DoodleMacWindow.h
        Engine/Runtime/Platform/MacOS/DoodleMacFileSystem.cpp
        Engine/Runtime/Platform/MacOS/DoodleMacFileSystem.h
        Engine/Runtime/Platform/MacOS/DoodleMacApplication.cpp
        Engine/Runtime/Platform/MacOS/DoodleMacApplication.h
)

# Android 专属：驱动层、窗口与文件来源实现
set(DOODLE_ANDROID_SOURCES
    Engine/Runtime/Platform/Android/DoodleAndroidApplication.cpp
    Engine/Runtime/Platform/Android/DoodleAndroidApplication.h
    Engine/Runtime/Platform/Android/DoodleAndroidFileSystem.cpp
    Engine/Runtime/Platform/Android/DoodleAndroidFileSystem.h
    Engine/Runtime/Platform/Android/DoodleAndroidWindow.cpp
    Engine/Runtime/Platform/Android/DoodleAndroidWindow.h
)

# Windows 专属：窗口与文件来源实现
set(DOODLE_WINDOWS_SOURCES
    Engine/Runtime/Platform/Windows/DoodleWindowsApplication.cpp
    Engine/Runtime/Platform/Windows/DoodleWindowsApplication.h
    Engine/Runtime/Platform/Windows/DoodleWindowsFileSystem.cpp
    Engine/Runtime/Platform/Windows/DoodleWindowsFileSystem.h
    Engine/Runtime/Platform/Windows/DoodleWindowsWindow.cpp
    Engine/Runtime/Platform/Windows/DoodleWindowsWindow.h
)

# 引擎自带的可选功能包。CameraDirector 只依赖 Engine，不依赖 GamePlay
set(DOODLE_PACKAGE_SOURCES
    Engine/Package/CameraDirector/DoodleCameraDirectorTypes.h
    Engine/Package/CameraDirector/DoodleCameraDirector.h
    Engine/Package/CameraDirector/DoodleCameraDirector.cpp
    Engine/Package/CameraDirector/DoodleVirtualCamera.h
    Engine/Package/CameraDirector/DoodleVirtualCamera.cpp
    Engine/Package/CameraDirector/DoodleCameraOffset.h
    Engine/Package/CameraDirector/DoodleCameraOffset.cpp
    Engine/Package/DoodlePackageRegistration.h
    Engine/Package/DoodlePackageRegistration.cpp
)

# 游戏层：只依赖 Engine，Engine 不反向依赖它。
# 平台入口在 App/Platform，负责在场景加载前调用 DoodleGame::RegisterComponents
set(DOODLE_GAMEPLAY_SOURCES
    GamePlay/DoodleGame.h
    GamePlay/DoodleGame.cpp
    GamePlay/DoodleFreeLookController.h
    GamePlay/DoodleFreeLookController.cpp
)

# 组合根：平台入口只属于可执行文件，不属于 Engine。
# App 是唯一同时认识 Engine 与 GamePlay 的层；各平台 main 在这里直接
# 调用 DoodleGame::RegisterComponents，把游戏接进引擎。
# Windows 的应用程序清单（DPI 感知与 UTF-8 代码页）也属于可执行文件：
# .manifest 列进源文件，MSVC 链接时就会把它嵌进 exe
set(DOODLE_APP_MACOS_SOURCES
    App/Platform/MacOS/DoodleMacMain.cpp
)
set(DOODLE_APP_WINDOWS_SOURCES
    App/Platform/Windows/DoodleWindowsMain.cpp
    App/Platform/Windows/DoodleGame.manifest
)
set(DOODLE_APP_ANDROID_SOURCES
    App/Platform/Android/DoodleAndroidMain.cpp
)

# ---- 着色器：编译 GLSL 并生成内嵌副本 ----
#
# 这是构建的一步，不是手动步骤。
# 原先的安排是「改完 GLSL 手动跑 Shaders/compile.sh」，它有个很坏的失败模式：
# 忘了跑 → 悄悄用着上一次的旧字节码 → 画面不对但没有任何报错，很难查到根上。
# 现在把 GLSL 源码列为依赖，改了就会自动重编。
#
# 内嵌副本放构建目录而不是源码目录：构建产物不落到源码树里，
# 桌面和 Android 两个构建目录也就不会互相踩

set(_doodle_shader_sources
    "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/shader.vert"
    "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/shader.frag"
)
set(_doodle_shader_output_dir "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(_doodle_embedded_shaders "${_doodle_shader_output_dir}/DoodleEmbeddedShaders.h")

# 用哪个 glslc：
#   Android —— NDK 自带一份（shader-tools/<host>/glslc），
#              这样 Android 构建不依赖机器上装没装 Vulkan SDK
#   Windows —— Vulkan SDK 的 Bin 目录，按 VULKAN_SDK 环境变量先找。下面 find_package(Vulkan)
#              找头文件和库也认这个变量，两边用的是同一份 SDK；PATH 上要是还挂着别的
#              glslc（旧版 SDK、NDK 自带的），不会被它抢先
#   macOS   —— PATH 上的那一份，来自 Vulkan SDK
if(ANDROID)
    set(_doodle_glslc "${ANDROID_NDK}/shader-tools/${ANDROID_HOST_TAG}/glslc")
elseif(WIN32)
    find_program(_doodle_glslc NAMES glslc HINTS "$ENV{VULKAN_SDK}/Bin" REQUIRED)
else()
    find_program(_doodle_glslc NAMES glslc REQUIRED)
endif()

# 谁来跑 compile.sh。
#
# macOS / Linux 上它自己就是可执行文件；Windows 主机没有这个能力，得显式交给 bash ——
# 用 Git for Windows 自带的那份，脚本要用的 xxd 它也带了。
#
# 只认 Git 安装目录下的 bin/bash.exe，不去 PATH 上找：
#   - C:\Windows\System32\bash.exe 是 WSL 的入口，跑起来是另一个 Linux，
#     认不得 D:/ 这种路径，也调不动 Windows 上的 glslc.exe
#   - Git 自己的 usr/bin/bash.exe 不会把 /usr/bin 加进 PATH，脚本里的 sed、xxd 全找不到；
#     bin/bash.exe 是它的启动器，先把环境搭好再起 bash（两个都实测过）
#
# 判据是主机而不是目标平台（CMAKE_HOST_WIN32 而不是 WIN32）：在 Windows 上
# 构建 Android 时，跑这个脚本的同样是这台 Windows
set(_doodle_shader_script_launcher "")
if(CMAKE_HOST_WIN32)
    # 两条线索：Git for Windows 安装时写进注册表的安装目录；以及 git.exe 的位置 ——
    # 它可能在 <Git>/cmd、<Git>/bin 或 <Git>/mingw64/bin，往上一到两级就是 <Git>。
    # 注册表那条只能直接写在 find_program 里：它带着分号，经列表变量转一手会被拆成两截
    set(_doodle_git_bash_hints "")
    find_package(Git QUIET)
    if(GIT_EXECUTABLE)
        get_filename_component(_doodle_git_executable_dir "${GIT_EXECUTABLE}" DIRECTORY)
        list(APPEND _doodle_git_bash_hints
            "${_doodle_git_executable_dir}/../bin"
            "${_doodle_git_executable_dir}/../../bin"
        )
    endif()

    find_program(_doodle_bash NAMES bash
        HINTS
            "[HKEY_LOCAL_MACHINE\\SOFTWARE\\GitForWindows;InstallPath]/bin"
            ${_doodle_git_bash_hints}
        NO_DEFAULT_PATH
    )
    if(NOT _doodle_bash)
        message(FATAL_ERROR
            "找不到 Git for Windows 自带的 bash。编译着色器的 Shaders/compile.sh 要用它。\n"
            "装好 Git for Windows（https://git-scm.com/download/win）之后重新生成工程；"
            "装在非标准位置时用 -D_doodle_bash=<Git 安装目录>/bin/bash.exe 指定。")
    endif()
    set(_doodle_shader_script_launcher "${_doodle_bash}")
endif()

add_custom_command(
    OUTPUT "${_doodle_embedded_shaders}"
    COMMAND ${CMAKE_COMMAND} -E env "GLSLC=${_doodle_glslc}"
            ${_doodle_shader_script_launcher}
            "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/compile.sh" "${_doodle_shader_output_dir}"
    DEPENDS ${_doodle_shader_sources}
            "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/compile.sh"
    COMMENT "Compile GLSL shaders and embed them"
    VERBATIM
)
list(APPEND DOODLE_SHARED_SOURCES "${_doodle_embedded_shaders}")

# ============================================================================
# Android
#
# 这段靠 ANDROID 变量判断 —— 它不是 CMake 内置的，是 NDK 的
# android.toolchain.cmake 注入的。构建方式见 android/ 下的 Gradle 工程
# ============================================================================
if(ANDROID)
    # NDK 自带的胶水层，随包只发源码不发库，得自己编进去。
    # 它提供 ANativeActivity_onCreate 与 android_main 之间的那层连接：
    # Looper、事件队列、窗口命令（APP_CMD_INIT_WINDOW 之类）都出自这里
    set(_doodle_native_app_glue_dir "${ANDROID_NDK}/sources/android/native_app_glue")

    # 引擎静态库：Vulkan、资产、场景、平台适配器。
    # 这里没有 GamePlay，也没有 android_main —— 依赖方向是单向的
    add_library(DoodleEngine STATIC
        ${DOODLE_SHARED_SOURCES}
        ${DOODLE_ANDROID_SOURCES}
        ${DOODLE_PACKAGE_SOURCES}
    )

    # Android 侧出的是 .so 而不是可执行文件：进程由系统 fork 出来，
    # 入口是 ANativeActivity_onCreate，android_main 由胶水层回调。
    # App + GamePlay 编进这个 .so，再链接引擎
    add_library(DoodleGame SHARED
        ${DOODLE_APP_ANDROID_SOURCES}
        ${DOODLE_GAMEPLAY_SOURCES}
        "${_doodle_native_app_glue_dir}/android_native_app_glue.c"
    )

    # 别把工程根目录（CMAKE_CURRENT_SOURCE_DIR 本身）加进来。
    # 理由在下面桌面分支那段注释里写全了，两边是同一个坑
    #
    # Engine/ThirdParty 里是仓库自带的第三方库（目前只有 glm/）。Android 侧没有 brew、
    # 也没有包管理器，系统路径上不会有它们 —— 收进仓库才有得用，
    # 这也是两个平台能共用同一版本的原因
    #
    # 这些路径放在 PUBLIC：游戏层和 App 的 main 要 include Engine 的公开头文件。
    # GamePlay 的路径只加给 DoodleGame，引擎拿不到 —— 反向依赖在编译期就失败
    target_include_directories(DoodleEngine PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/Engine/ThirdParty
        ${CMAKE_CURRENT_SOURCE_DIR}/Engine/Runtime
        ${CMAKE_CURRENT_SOURCE_DIR}/Engine/Package
        ${_doodle_native_app_glue_dir}
    )
    target_include_directories(DoodleEngine PRIVATE
        ${_doodle_shader_output_dir}
    )
    target_include_directories(DoodleGame PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/App
        ${CMAKE_CURRENT_SOURCE_DIR}/GamePlay
    )

    # Vulkan 头文件里那些平台类型（VkAndroidSurfaceCreateInfoKHR、
    # vkCreateAndroidSurfaceKHR）都在 #ifdef VK_USE_PLATFORM_ANDROID_KHR 里，
    # 而 NDK 的 vk_platform.h 不会自动开这个宏 —— 不定义就是编译错误。
    # 桌面端的对应物是 GLFW 的 GLFW_INCLUDE_VULKAN，同样得有人定义。
    # 引擎的公开头文件里会出现 Vulkan 类型，所以这个定义要传给使用方
    target_compile_definitions(DoodleEngine PUBLIC VK_USE_PLATFORM_ANDROID_KHR)

    # libvulkan.so 由 NDK 的 sysroot 提供（Android 上 Vulkan 是系统库）。
    # android / log 是 NDK 的 platform 库
    target_link_libraries(DoodleEngine PRIVATE
        vulkan
        android
        log
    )
    target_link_libraries(DoodleGame PRIVATE
        DoodleEngine
        vulkan
        android
        log
    )

    # 静态库最终要进 .so，必须按位置无关代码编
    set_target_properties(DoodleEngine PROPERTIES POSITION_INDEPENDENT_CODE ON)

    # ANativeActivity_onCreate 定义在胶水里，但没有任何地方引用它 ——
    # 框架是在运行时 dlopen + dlsym 按名字找的。不把它钉住的话，
    # 链器会认为这个符号没人用而丢掉，结果是 APK 装得上、点不开。
    # 注意这条要配合 project() 里启用了 C 语言才成立，理由见那里的注释
    target_link_options(DoodleGame PRIVATE
        "-u" "ANativeActivity_onCreate"
    )

    return()
endif()

# ============================================================================
# Windows
#
# 产出 Visual Studio 的 sln：windows/generate.bat 用 CMake 的 Visual Studio 生成器生成，
# 落在 windows/build/ 下。工程的源头仍然只有这一份 CMakeLists.txt —— 与 android/ 下的
# Gradle 工程是同一个关系，加源文件改的还是上面的清单。sln 是生成物，不入库；
# 改了本文件之后不用重跑脚本，VS 里一构建，ZERO_CHECK 会先把工程重新生成一遍
#
# 依赖只有两样：Visual Studio（带「使用 C++ 的桌面开发」工作负载）与 Vulkan SDK，
# 编着色器另外借用 Git for Windows 的 bash（见上面的着色器那段）。
# 窗口直接用 Win32，不需要 GLFW，理由见 DoodleWindowsWindow.h。
# 最低 Windows 10 1607：GetDpiForWindow / AdjustWindowRectExForDpi 从那一版开始才有
#
# generate.bat 通篇只用 ASCII，注释与提示都是英文，这是刻意的：cmd 读含多字节 UTF-8
# 字符的批处理时会算错下一行的文件偏移，把注释行的后半截当成命令执行。简中控制台（936）
# 上实测：同一份脚本直接 call 一切正常，经管道调用时就开始乱跑 —— 开头 chcp 65001 也救不了
# 用法、前置条件都写在那份脚本的开头
# ============================================================================
if(WIN32)
    # Vulkan SDK 安装时设了 VULKAN_SDK 环境变量，FindVulkan 按它找头文件和 vulkan-1.lib。
    # 运行时的 vulkan-1.dll 由显卡驱动装进系统目录，不用随程序拷贝 ——
    # 这点和 macOS 要把 libvulkan 拷进 .app 不同
    find_package(Vulkan REQUIRED)

    # 引擎静态库：Vulkan、资产、场景、平台适配器。
    # 这里没有 GamePlay —— 引擎的 include 路径里也没有 GamePlay，
    # 反向依赖在编译期就会失败
    add_library(DoodleEngine STATIC
        ${DOODLE_SHARED_SOURCES}
        ${DOODLE_WINDOWS_SOURCES}
        ${DOODLE_PACKAGE_SOURCES}
    )

    # 着色器源码也挂进工程，只为在解决方案资源管理器里能直接打开编辑。
    # 它们不是 C++ 源文件，VS 不会去编；编它们的是上面那条自定义命令
    add_executable(DoodleGame
        ${DOODLE_APP_WINDOWS_SOURCES}
        ${DOODLE_GAMEPLAY_SOURCES}
        ${_doodle_shader_sources}
        "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/compile.sh"
    )

    # 与 Android 那段同一个规矩：不加工程根目录。
    # Vulkan 的头文件由下面链接的 Vulkan::Vulkan 带进来
    target_include_directories(DoodleEngine PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/Engine/ThirdParty
        ${CMAKE_CURRENT_SOURCE_DIR}/Engine/Runtime
        ${CMAKE_CURRENT_SOURCE_DIR}/Engine/Package
    )
    target_include_directories(DoodleEngine PRIVATE
        ${_doodle_shader_output_dir}
    )
    target_include_directories(DoodleGame PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/App
        ${CMAKE_CURRENT_SOURCE_DIR}/GamePlay
    )

    # VK_USE_PLATFORM_WIN32_KHR —— 与 Android 的 VK_USE_PLATFORM_ANDROID_KHR 同理：
    #   VkWin32SurfaceCreateInfoKHR、vkCreateWin32SurfaceKHR 都在这个宏后面
    # WIN32_LEAN_AND_MEAN —— <windows.h> 不再顺带拉进 winsock、OLE 那一大批用不上的头
    # NOMINMAX —— <windows.h> 默认定义 min / max 两个宏，会把 std::min 和
    #   std::numeric_limits<T>::max() 改写坏，共用代码里两处都有
    # UNICODE / _UNICODE —— Win32 里不带 A / W 后缀的那一套（IDC_ARROW 这类资源宏、
    #   以及将来顺手写的 MessageBox 之类）一律落到宽字符版本。VS 新建工程默认就是这样，
    #   CMake 生成的工程却不带；不定义的话它们落到 A 版本，按系统代码页解释字符串
    #
    # 前面这几个只能挡住宏的一部分。CreateWindow 这类与函数同名的宏挡不掉，
    # 靠的是「<windows.h> 只在 .cpp 里、且排在本工程头文件之后」，见 DoodleWindowsWindow.h
    #
    # 放在引擎的 PUBLIC：App 的 main 也要包含 <windows.h>（控制台代码页），
    # GamePlay 又包含引擎的公开头文件，定义必须一致
    target_compile_definitions(DoodleEngine PUBLIC
        VK_USE_PLATFORM_WIN32_KHR
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        UNICODE
        _UNICODE
    )

    if(MSVC)
        # 源码是不带 BOM 的 UTF-8，而 MSVC 默认按系统代码页读源文件，简中系统上是 936。
        # 轻则满屏 C4819；重则 // 注释行尾汉字落单的那个字节被当成双字节字符的前半，
        # 把后面的换行吞掉，下一行代码跟着成了注释 —— 报出来的错和根因毫不相干。
        # /utf-8 同时把执行字符集定成 UTF-8，字符串字面量在各平台上是同一份字节
        target_compile_options(DoodleEngine PRIVATE /utf-8)
        target_compile_options(DoodleGame PRIVATE /utf-8)
    endif()

    if(CMAKE_GENERATOR MATCHES "Visual Studio")
        # VS 生成器默认在一个工程里逐个编 .cpp，打开 /MP 才会并行
        target_compile_options(DoodleEngine PRIVATE /MP)
        target_compile_options(DoodleGame PRIVATE /MP)
    endif()

    # Vulkan 的头文件与库都通过引擎的公开依赖传给游戏目标
    target_link_libraries(DoodleEngine PUBLIC Vulkan::Vulkan)
    target_link_libraries(DoodleGame PRIVATE DoodleEngine)

    # ---- Visual Studio 工程的几处设置 ----

    # 启动项目。不设的话 VS 打开 sln 默认选中 ALL_BUILD，按 F5 只会报「无法启动程序」
    set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT DoodleGame)

    # 调试时的工作目录 = 可执行文件所在目录（默认是 vcxproj 所在的构建目录）。
    # 下面会把运行时数据拷到那里，于是按 F5、在资源管理器里双击、从命令行别处启动，
    # 读到的都是同一份（查找顺序见 DoodleWindowsFileSystem.h）
    set_target_properties(DoodleGame PROPERTIES
        VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:DoodleGame>"
    )

    # 解决方案资源管理器里按磁盘上的目录分组，与 CLion 看到的工程树一致，
    # 而不是全堆进「源文件 / 头文件」两个筛选器。
    # 着色器内嵌副本在构建目录里、不在源码树下，TREE 收不了它，单放一组
    set(_doodle_source_tree ${DOODLE_SHARED_SOURCES} ${DOODLE_WINDOWS_SOURCES} ${DOODLE_PACKAGE_SOURCES} ${DOODLE_GAMEPLAY_SOURCES} ${DOODLE_APP_WINDOWS_SOURCES} ${_doodle_shader_sources})
    list(REMOVE_ITEM _doodle_source_tree "${_doodle_embedded_shaders}")
    source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES
        ${_doodle_source_tree}
        "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/compile.sh"
    )
    source_group("Generated" FILES "${_doodle_embedded_shaders}")

    # ALL_BUILD、ZERO_CHECK 这些 CMake 自带的工程收进一个文件夹，不和 DoodleGame 并列
    set_property(GLOBAL PROPERTY USE_FOLDERS ON)

    # ---- 运行时数据：摆到可执行文件旁边 ----
    #
    # 对应 macOS 往 .app/Contents/Resources 里拷的那两份。Windows 没有包结构，
    # 按惯例和可执行文件放在一起：
    #   <输出目录>/Shaders/*.spv
    #   <输出目录>/Scenes/
    #
    # 着色器只拷 .spv，而且直接从构建目录拷。macOS 那边要先把 .spv 摆进源码树的 Shaders/，
    # 是因为 CLion 的工作目录是工程根目录；这边调试工作目录就是输出目录，
    # 不需要往源码树里写东西。从别的目录启动时 DoodleShaderModule 找不到这两个文件，
    # 会退到编进二进制的内嵌副本，那份同样是本次构建的产物，不会过期
    #
    # 和 macOS 同理，两份都先删再拷：copy_directory 只增不删，源码侧删掉的文件
    # 会在输出目录里活下来，运行时优先命中陈旧副本
    add_custom_command(TARGET DoodleGame POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E rm -rf "$<TARGET_FILE_DIR:DoodleGame>/Shaders"
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:DoodleGame>/Shaders"
        COMMAND ${CMAKE_COMMAND} -E copy
            "${_doodle_shader_output_dir}/vert.spv"
            "${_doodle_shader_output_dir}/frag.spv"
            "$<TARGET_FILE_DIR:DoodleGame>/Shaders"
        COMMENT "Copy shaders next to DoodleGame.exe"
        VERBATIM
    )

    # 场景文件同样不能只挂 POST_BUILD：改 JSON 不会触发链接，POST_BUILD 不跑，
    # 输出目录里会一直留着旧场景。这里改成按文件依赖触发，stamp 保证无变化时
    # 什么都不做。让 DoodleGame 依赖这个目标，单独构建 DoodleGame 时也会执行
    file(GLOB _doodle_scene_files CONFIGURE_DEPENDS
        "${CMAKE_SOURCE_DIR}/Resources/Scenes/*")
    set(_doodle_scene_stamp "${CMAKE_CURRENT_BINARY_DIR}/doodle_scenes_windows.stamp")

    add_custom_command(
        OUTPUT "${_doodle_scene_stamp}"
        COMMAND ${CMAKE_COMMAND} -E rm -rf "$<TARGET_FILE_DIR:DoodleGame>/Scenes"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/Resources/Scenes"
            "$<TARGET_FILE_DIR:DoodleGame>/Scenes"
        COMMAND ${CMAKE_COMMAND} -E touch "${_doodle_scene_stamp}"
        DEPENDS ${_doodle_scene_files}
        COMMENT "Copy Scenes next to DoodleGame.exe"
        VERBATIM
    )

    add_custom_target(doodle_stage_scenes DEPENDS "${_doodle_scene_stamp}")
    add_dependencies(DoodleGame doodle_stage_scenes)

    # Art 运行数据（FBX 等）与场景同理：改模型不会触发链接，必须按文件依赖
    # 拷贝。输出目录布局与 macOS 的包内 Resources 对齐，代码里的相对路径
    # 仍是 "Art/..."
    file(GLOB_RECURSE _doodle_art_files CONFIGURE_DEPENDS
        "${CMAKE_SOURCE_DIR}/Resources/Art/*")
    set(_doodle_art_stamp "${CMAKE_CURRENT_BINARY_DIR}/doodle_art_windows.stamp")

    add_custom_command(
        OUTPUT "${_doodle_art_stamp}"
        COMMAND ${CMAKE_COMMAND} -E rm -rf "$<TARGET_FILE_DIR:DoodleGame>/Art"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/Resources/Art"
            "$<TARGET_FILE_DIR:DoodleGame>/Art"
        COMMAND ${CMAKE_COMMAND} -E touch "${_doodle_art_stamp}"
        DEPENDS ${_doodle_art_files}
        COMMENT "Copy Art next to DoodleGame.exe"
        VERBATIM
    )

    add_custom_target(doodle_stage_art DEPENDS "${_doodle_art_stamp}")
    add_dependencies(DoodleGame doodle_stage_art)

    return()
endif()

# ============================================================================
# macOS（桌面）
# ============================================================================

# 第三方头文件 / 库的搜索目录。
# Vulkan SDK 在 /usr/local，Homebrew 的 GLFW / GLM 在 /opt/homebrew。
#
# 加的是 Engine/ThirdParty 本身、不是 Engine/ThirdParty/glm —— 一个库一个目录，
# 路径加父目录，这样 #include <glm/glm.hpp> 解析到 Engine/ThirdParty/glm/glm.hpp。
# 将来再引别的（volk 之类）照同一规矩放，不必再加路径
#
# 仓库自带的那份排在最前。这台机器上 brew 也装了一份同名同版本的 GLM，
# 顺序反了会静默用到系统那份 —— 你以为在用仓库里锁定的版本，实际不是，
# 换台机器行为就可能不同（见 Engine/ThirdParty/glm/README.md）
set(DOODLE_HEADER_SEARCH_PATHS
    ${CMAKE_SOURCE_DIR}/Engine/ThirdParty
    /usr/local/include
    /opt/homebrew/include
    ${CMAKE_SOURCE_DIR}/Engine/Runtime
)
set(DOODLE_LIBRARY_SEARCH_PATHS
    /usr/local/lib
    /opt/homebrew/lib
)

# CMAKE_PREFIX_PATH 是 CMake 找「已安装第三方包」的总入口。插到最前面，优先用这两处。
# 真正找库的是下面的 find_library，这行是备用。
list(PREPEND CMAKE_PREFIX_PATH /usr/local /opt/homebrew)

# 在指定目录里找 dylib，命中后把完整路径存进第一个参数这个变量。
#   NAMES           = 依次试这些名字（macOS 会自动补成 libxxx.dylib）
#   PATHS           = 只在上面的库搜索目录里找
#   NO_DEFAULT_PATH = 不去 CMake 默认系统目录瞎找
#   REQUIRED        = 找不到就立刻失败
# libvulkan.1.dylib 是指向 libvulkan.1.4.357.dylib 的符号链接，链一份即可。
find_library(GLFW_LIBRARY
    NAMES glfw.3.5 glfw.3 glfw
    PATHS ${DOODLE_LIBRARY_SEARCH_PATHS}
    NO_DEFAULT_PATH
    REQUIRED
)
find_library(VULKAN_LIBRARY
    NAMES vulkan.1 vulkan
    PATHS ${DOODLE_LIBRARY_SEARCH_PATHS}
    NO_DEFAULT_PATH
    REQUIRED
)

# 原先用 find_path 分别定位 GLFW / Vulkan / GLM 的头文件根目录，
# 结果就是 DOODLE_HEADER_SEARCH_PATHS 里的那两个路径，所以改成直接用它。
# 这段先留着对照，不参与配置。
# find_path 找的不是 .h 文件本身，而是「包含这个相对路径的那个根目录」。
# 源码写 #include <GLFW/glfw3.h>，所以要找到能拼出 GLFW/glfw3.h 的目录（/opt/homebrew/include）。
# GLM 是 header-only，没有 .dylib，所以只 find_path，不 find_library。
# find_path(GLFW_INCLUDE_DIR GLFW/glfw3.h
#     PATHS ${DOODLE_HEADER_SEARCH_PATHS}
#     NO_DEFAULT_PATH
#     REQUIRED
# )
# find_path(VULKAN_INCLUDE_DIR vulkan/vulkan.h
#     PATHS ${DOODLE_HEADER_SEARCH_PATHS}
#     NO_DEFAULT_PATH
#     REQUIRED
# )
# find_path(GLM_INCLUDE_DIR glm/glm.hpp
#     PATHS ${DOODLE_HEADER_SEARCH_PATHS}
#     NO_DEFAULT_PATH
#     REQUIRED
# )

# 引擎静态库：Vulkan、资产、场景、平台适配器。
# 这里没有 GamePlay —— 引擎的 include 路径里也没有 GamePlay，
# 反向依赖在编译期就会失败
add_library(DoodleEngine STATIC
    ${DOODLE_SHARED_SOURCES}
    ${DOODLE_MACOS_SOURCES}
    ${DOODLE_PACKAGE_SOURCES}
)

# 创建一个叫 DoodleGame 的可执行文件 target，并列出源文件。
# MACOSX_BUNDLE = 在 macOS 上打成 DoodleGame.app，而不是裸的可执行文件。
#   产物结构：DoodleGame.app/Contents/MacOS/DoodleGame
#             DoodleGame.app/Contents/Frameworks/*.dylib
#             DoodleGame.app/Contents/Info.plist（CMake 自动生成）
# .cpp 会真正编译；.h 写进来是让 CLion 显示在工程树里、改头文件时能触发重编。
# 路径相对本 CMakeLists.txt 所在目录。以后加新 .cpp，改的就是上面的清单变量。
add_executable(DoodleGame MACOSX_BUNDLE
    ${DOODLE_APP_MACOS_SOURCES}
    ${DOODLE_GAMEPLAY_SOURCES}
)

# 把头文件搜索路径接到引擎上（PUBLIC：游戏层和 App 的 main 要 include 引擎头文件）。
# 第三方头文件直接用上面的搜索目录，不再走 find_path；
# Engine/Runtime/ 也在那里面，所以 #include "Vulkan/xxx.h"、"Platform/xxx.h" 这类能解析
# （平台文件夹内部之间的引用走 "../xxx.h" 相对路径，不经过这条）。
# _doodle_shader_output_dir 是着色器内嵌副本的生成位置，见上面的 add_custom_command。
#
# 这里**不要**再放工程根目录（CMAKE_SOURCE_DIR 本身）。
# 曾经有一行，理由是「这样 #include "Engine/Runtime/xxx.h" 才能从根目录拼出来」，
# 但工程里根本没有任何一处这样写 —— 全是 "Vulkan/xxx.h" 或 "xxx.h" 这种形式。
# 它的实际效果是把根目录下所有东西都当成头文件搜索路径，
# Android Studio 的工程视图会把 .git、.idea、cmake-build-debug 这些
# 一并当成「构建相关目录」列进 includes 分组里，一屏全是噪声
#
# GamePlay 的路径只加给 DoodleGame，引擎拿不到
target_include_directories(DoodleEngine PUBLIC
    ${DOODLE_HEADER_SEARCH_PATHS}
    ${CMAKE_CURRENT_SOURCE_DIR}/Engine/Package
)
target_include_directories(DoodleEngine PRIVATE
    ${_doodle_shader_output_dir}
)
target_include_directories(DoodleGame PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/App
    ${CMAKE_CURRENT_SOURCE_DIR}/GamePlay
)

# 把找到的两个 dylib 链进可执行文件。
# 链接器会记下 GLFW 的绝对路径，以及 Vulkan 的 @rpath/libvulkan.1.dylib（运行时再按 rpath 找）。
target_link_libraries(DoodleEngine PRIVATE
    ${GLFW_LIBRARY}
    ${VULKAN_LIBRARY}
)
target_link_libraries(DoodleGame PRIVATE
    DoodleEngine
    ${GLFW_LIBRARY}
    ${VULKAN_LIBRARY}
)

# 链接成功 ≠ 运行成功。macOS 启动时还要再找一遍动态库。
# Vulkan loader 把自己的名字写成了 @rpath/libvulkan.1.dylib，意思是「到这个 exe 的 rpath 列表里找我」。
# .app 里可执行文件在 Contents/MacOS/，dylib 拷在 Contents/Frameworks/，所以：
#   @executable_path/../Frameworks = 先看包内 Frameworks
#   /usr/local/lib                 = 再看 Vulkan SDK 原位
#   /opt/homebrew/lib              = 再看 Homebrew
# BUILD_RPATH   = 在 cmake-build-debug 里直接跑时用
# INSTALL_RPATH = 如果以后 cmake --install 装到别处时用
# 下划线开头的 _doodle_rpath 只是临时变量，不是 CMake 内置名。
set(_doodle_rpath
    "@executable_path/../Frameworks"
    /usr/local/lib
    /opt/homebrew/lib
)
set_target_properties(DoodleGame PROPERTIES
    BUILD_RPATH "${_doodle_rpath}"
    INSTALL_RPATH "${_doodle_rpath}"
    BUILD_RPATH_USE_ORIGIN ON
    MACOSX_BUNDLE_BUNDLE_NAME DoodleGame
    MACOSX_BUNDLE_GUI_IDENTIFIER com.doodle.DoodleGame
)

# 每次链接成功后，把这 3 个 dylib 拷进 .app 的 Contents/Frameworks/，
# 这样 @executable_path/../Frameworks 才能找到它们。
# foreach = 对列表里每个文件做一次。
# ${CMAKE_COMMAND} -E copy_if_different = 用 CMake 自带的「不一样才拷」，不手写 cp。
# $<TARGET_BUNDLE_CONTENT_DIR:DoodleGame> 是 generator expression，
# 生成 build 文件时才展开成 DoodleGame.app/Contents。
# libvulkan.1.dylib 是指向 libvulkan.1.4.357.dylib 的符号链接，两个都要拷，少一个运行时会断。
if(APPLE)
    set(_doodle_bundled_dylibs
        /usr/local/lib/libvulkan.1.dylib
        /usr/local/lib/libvulkan.1.4.357.dylib
        /opt/homebrew/lib/libglfw.3.5.dylib
    )
    add_custom_command(TARGET DoodleGame POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_BUNDLE_CONTENT_DIR:DoodleGame>/Frameworks"
        COMMENT "Create DoodleGame.app/Contents/Frameworks"
    )
    foreach(_dylib IN LISTS _doodle_bundled_dylibs)
        add_custom_command(TARGET DoodleGame POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_dylib}"
                "$<TARGET_BUNDLE_CONTENT_DIR:DoodleGame>/Frameworks"
            COMMENT "Copy ${_dylib} into DoodleGame.app/Contents/Frameworks"
        )
    endforeach()

    # 把刚编好的 SPIR-V 摆到**运行时真正会去读**的目录。
    #
    # 着色器字节码有三个可能的来源，查找顺序见 DoodleShaderModule::ReadFile：
    #   ① 进程当前工作目录下的路径 —— CLion 里跑工程时工作目录就是工程根目录，
    #      于是命中的是 Shaders/ 下的那一份
    #   ② <可执行文件目录>/../Resources/Shaders/ —— Finder 双击时走这条
    #   ③ 编进二进制的内嵌副本 —— Android 只能走这条
    #
    # 上面那个 add_custom_command 把 .spv 生成在**构建目录**里（内嵌副本也从那儿来）。
    # 构建不往 ① ② 写的话，那两个位置就只能靠人手动放 —— 而一改着色器源码，
    # 手动放的那份就悄悄过期，没有任何报错，程序照着旧的画。这里显式摆过去，
    # 让 ① 永远是最新的；下面那条 copy_directory 再把它带进包的 ②。
    #
    # 只在桌面上做，两个理由：
    #   - Android 运行时够不着文件系统（APK 里的东西不是普通文件），它走 ③
    #   - 两个平台用的 glslc 不是同一份（这边 PATH 上的，Android 那边 NDK 自带的），
    #     编出来的字节并不相同。让 Android 也写这里会互相覆盖，
    #     最后编了哪个平台，Shaders/ 里就是哪个平台的字节码
    #
    # 产物落在源码树里是刻意的 —— 它是「运行时数据」而不是构建中间物，
    # 运行时就认这个路径。.gitignore 里已经排除了 *.spv
    add_custom_command(
        OUTPUT
            "${CMAKE_SOURCE_DIR}/Shaders/vert.spv"
            "${CMAKE_SOURCE_DIR}/Shaders/frag.spv"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${_doodle_shader_output_dir}/vert.spv"
            "${CMAKE_SOURCE_DIR}/Shaders/vert.spv"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${_doodle_shader_output_dir}/frag.spv"
            "${CMAKE_SOURCE_DIR}/Shaders/frag.spv"
        DEPENDS "${_doodle_embedded_shaders}"
        COMMENT "Stage shader SPIR-V into Shaders/ for the desktop runtime"
        VERBATIM
    )

    add_custom_target(doodle_stage_shaders ALL
        DEPENDS
            "${CMAKE_SOURCE_DIR}/Shaders/vert.spv"
            "${CMAKE_SOURCE_DIR}/Shaders/frag.spv"
    )

    # 摆位必须排在包内拷贝之前，否则拷进包里的还是上一轮的旧文件
    add_dependencies(DoodleGame doodle_stage_shaders)

    # 把 Shaders/ 拷进 .app 的 Contents/Resources/，让包自带着色器字节码。
    # 运行时查找顺序见上（① 工作目录 → ② 包内 → ③ 内嵌）。
    # 之所以放在包内 Resources 而不是可执行文件旁边，是遵循 macOS 的包约定。
    # copy_directory 会把 .vert/.frag 源码一并拷进去，体积可忽略；
    # 若以后不想随包分发源码，改成只拷 *.spv 即可
    #
    # 先整个删掉再拷：copy_directory 只增不删，目标端多出来的文件会一直留着。
    # 这正是踩过的那个坑 —— 源码侧删掉的旧 .spv，在包里活了下来，
    # 而运行时先找到包里那份，于是「删了却还在生效」
    add_custom_command(TARGET DoodleGame POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E rm -rf
            "$<TARGET_BUNDLE_CONTENT_DIR:DoodleGame>/Resources/Shaders"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/Shaders"
            "$<TARGET_BUNDLE_CONTENT_DIR:DoodleGame>/Resources/Shaders"
        COMMENT "Copy Shaders into DoodleGame.app/Contents/Resources/Shaders"
    )

    # 场景文件是运行时数据，和 Shaders 一样拷进包内 Resources。
    #
    # 这不只是 Finder 双击时需要：GLFW 在 macOS 上默认把进程工作目录切到
    # .app/Contents/Resources（GLFW_COCOA_CHDIR_RESOURCES），所以即使从终端
    # 直接运行包内可执行文件，相对路径解析的起点也是 Resources。工程根目录
    # 里的 Resources/Scenes 只是源文件。
    #
    # 与 Shaders 同理，先删再拷：copy_directory 只增不删，源码侧删掉的旧
    # 场景文件会在包里活下来，运行时优先命中陈旧副本。
    #
    # 但这里不能只挂 POST_BUILD：POST_BUILD 只在目标重新链接时执行，而改
    # JSON 不会触发链接 —— Ninja 会报 "no work to do"，包里一直留着旧场景。
    # 所以把场景文件列成依赖，内容变了就重拷；stamp 让没有变化时什么都不做。
    # 让 DoodleGame 依赖这个目标：无论只构建 DoodleGame 还是构建 all，拷贝
    # 都在同一条依赖链上。目标里先建目录再拷，干净构建时 .app 不存在也能跑
    file(GLOB _doodle_scene_files CONFIGURE_DEPENDS
        "${CMAKE_SOURCE_DIR}/Resources/Scenes/*")
    set(_doodle_scene_stamp "${CMAKE_CURRENT_BINARY_DIR}/doodle_scenes_macos.stamp")

    add_custom_command(
        OUTPUT "${_doodle_scene_stamp}"
        COMMAND ${CMAKE_COMMAND} -E rm -rf
            "$<TARGET_BUNDLE_CONTENT_DIR:DoodleGame>/Resources/Scenes"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/Resources/Scenes"
            "$<TARGET_BUNDLE_CONTENT_DIR:DoodleGame>/Resources/Scenes"
        COMMAND ${CMAKE_COMMAND} -E touch "${_doodle_scene_stamp}"
        DEPENDS ${_doodle_scene_files}
        COMMENT "Copy Scenes into DoodleGame.app/Contents/Resources/Scenes"
        VERBATIM
    )

    add_custom_target(doodle_stage_scenes DEPENDS "${_doodle_scene_stamp}")
    add_dependencies(DoodleGame doodle_stage_scenes)

    # Art 运行数据与场景同理：GLFW 把工作目录切到包内 Resources，代码里的
    # "Art/..." 必须能在 .app/Contents/Resources/Art 下找到。
    # 同样按源文件依赖触发，避免改模型后包里还是旧文件。
    file(GLOB_RECURSE _doodle_art_files CONFIGURE_DEPENDS
        "${CMAKE_SOURCE_DIR}/Resources/Art/*")
    set(_doodle_art_stamp "${CMAKE_CURRENT_BINARY_DIR}/doodle_art_macos.stamp")

    add_custom_command(
        OUTPUT "${_doodle_art_stamp}"
        COMMAND ${CMAKE_COMMAND} -E rm -rf
            "$<TARGET_BUNDLE_CONTENT_DIR:DoodleGame>/Resources/Art"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/Resources/Art"
            "$<TARGET_BUNDLE_CONTENT_DIR:DoodleGame>/Resources/Art"
        COMMAND ${CMAKE_COMMAND} -E touch "${_doodle_art_stamp}"
        DEPENDS ${_doodle_art_files}
        COMMENT "Copy Art into DoodleGame.app/Contents/Resources/Art"
        VERBATIM
    )

    add_custom_target(doodle_stage_art DEPENDS "${_doodle_art_stamp}")
    add_dependencies(DoodleGame doodle_stage_art)
endif()
