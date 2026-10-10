# 最低支持 CMake 3.22。
#
# 这份文件里没有任何 4.x 专有语法，用得最新的三处是：
#   find_library(... REQUIRED)    （3.18）
#   list(PREPEND)                 （3.15）
#   CMAKE_CXX_STANDARD 23 这个取值（3.20）
# 原先写 4.3（CLion 新建工程时按自带版本号填的），结果 Android SDK 自带的
# CMake 最高只到 4.1.2，两条 Android 路径全被挡住，反而只能依赖 CLion。
#
# 降到 3.22 之后两条链各有各的 CMake，互不干扰：
#   Android：SDK 的 3.22.1 + NDK clang，C++23 展开成 -std=gnu++2b（与 gnu++23 同义）
#   桌面   ：CLion 自带的 4.3.1
# 别拿 3.22.1 去建桌面 target —— 它的 AppleClang 表只到 C++20，
# 会把 C++23 报成「CMake does not know the compile flags」。
#
# 另外这一行不只是「声明最低版本」：它把该版本及之前的策略置为 NEW、
# 之后的保持 OLD，动这个数字等于动了 CMake 的策略基线。
cmake_minimum_required(VERSION 3.22)

# 工程名 / 主 target 名。后面 add_executable(DoodleGame ...) 必须和这里一致。
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

# 最低支持的 macOS 版本。只在 Mac 上生效。
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "15.5")
endif()

# ============================================================================
# 源文件清单
#
# 按「谁能编」分三摞。分开的理由是实的：GLFW 实现里全是 #include <GLFW/glfw3.h>，
# Android 侧根本没有这个库；反过来 Android 入口依赖 <android_native_app_glue.h>，
# 桌面也没有。混在一摞里只能靠 #ifdef 挡，那是把平台判断散到各处，
# 不如在这里一次说清
#
# 平台专属的那两摞，路径与 Runtime/Platform/<平台>/ 一一对应：
# 一个平台的窗口实现和它的入口都在自己那个文件夹里。
# Platform/ 根目录下的是三摞共用的那部分（接口、驱动层、日志）
#
# DoodleApplication 编进了共享那摞而不是 macOS 那摞：它不依赖任何平台 API。
# 两个平台都要用它 —— 它是「当前应用」这个概念本身（持有 Vulkan 子系统、
# 接收窗口的尺寸通知、被 C 回调经 GetInstance 找到），只是三个生命周期函数
# 带的那套默认实现以「窗口与程序同寿」为前提，所以 Android 另有一份覆盖它们的
# DoodleAndroidApplication（在 Android 那摞里）
# ============================================================================

# 与平台无关的部分：Vulkan 各层 + 窗口抽象 + 日志
set(DOODLE_SHARED_SOURCES
    Runtime/Vulkan/DoodleVulkanDevice.cpp
    Runtime/Vulkan/DoodleVulkanDevice.h
    Runtime/Vulkan/DoodleSwapChain.cpp
    Runtime/Vulkan/DoodleSwapChain.h
    Runtime/Vulkan/DoodleGraphicsPipeline.cpp
    Runtime/Vulkan/DoodleGraphicsPipeline.h
    Runtime/Vulkan/DoodleCommandBuffer.cpp
    Runtime/Vulkan/DoodleCommandBuffer.h
    Runtime/Vulkan/DoodleSyncObjects.cpp
    Runtime/Vulkan/DoodleSyncObjects.h
    Runtime/Vulkan/DoodleVulkanManager.cpp
    Runtime/Vulkan/DoodleVulkanManager.h
    Runtime/Vulkan/DoodleShaderModule.cpp
    Runtime/Vulkan/DoodleShaderModule.h
    Runtime/Vulkan/DoodleVulkanConfig.h
    Runtime/Vulkan/QueueFamilySelection.h
    Runtime/Vulkan/SwapChainSupportDetails.h
    Runtime/Platform/DoodleWindow.h
    Runtime/Platform/DoodleApplication.cpp
    Runtime/Platform/DoodleApplication.h
    Runtime/Platform/DoodleLog.cpp
    Runtime/Platform/DoodleLog.h
    Runtime/Utility/Singleton.h
)

# macOS 专属：入口、窗口实现
set(DOODLE_MACOS_SOURCES
        Runtime/Platform/MacOS/DoodleMacMain.cpp
        Runtime/Platform/MacOS/DoodleMacWindow.cpp
        Runtime/Platform/MacOS/DoodleMacWindow.h
        Runtime/Platform/MacOS/DoodleMacApplication.cpp
        Runtime/Platform/MacOS/DoodleMacApplication.h
)

# Android 专属：入口、驱动层实现、窗口实现
set(DOODLE_ANDROID_SOURCES
    Runtime/Platform/Android/DoodleAndroidMain.cpp
    Runtime/Platform/Android/DoodleAndroidApplication.cpp
    Runtime/Platform/Android/DoodleAndroidApplication.h
    Runtime/Platform/Android/DoodleAndroidWindow.cpp
    Runtime/Platform/Android/DoodleAndroidWindow.h
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
#   桌面    —— PATH 上的那一份，来自 Vulkan SDK
if(ANDROID)
    set(_doodle_glslc "${ANDROID_NDK}/shader-tools/${ANDROID_HOST_TAG}/glslc")
else()
    find_program(_doodle_glslc NAMES glslc REQUIRED)
endif()

add_custom_command(
    OUTPUT "${_doodle_embedded_shaders}"
    COMMAND ${CMAKE_COMMAND} -E env "GLSLC=${_doodle_glslc}"
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

    # Android 侧出的是 .so 而不是可执行文件：进程由系统 fork 出来，
    # 入口是 ANativeActivity_onCreate，android_main 由胶水层回调
    add_library(DoodleGame SHARED
        ${DOODLE_SHARED_SOURCES}
        ${DOODLE_ANDROID_SOURCES}
        "${_doodle_native_app_glue_dir}/android_native_app_glue.c"
    )

    # 别把工程根目录（CMAKE_CURRENT_SOURCE_DIR 本身）加进来。
    # 理由在下面桌面分支那段注释里写全了，两边是同一个坑
    target_include_directories(DoodleGame PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/Runtime
        ${_doodle_shader_output_dir}
        ${_doodle_native_app_glue_dir}
    )

    # Vulkan 头文件里那些平台类型（VkAndroidSurfaceCreateInfoKHR、
    # vkCreateAndroidSurfaceKHR）都在 #ifdef VK_USE_PLATFORM_ANDROID_KHR 里，
    # 而 NDK 的 vk_platform.h 不会自动开这个宏 —— 不定义就是编译错误。
    # 桌面端的对应物是 GLFW 的 GLFW_INCLUDE_VULKAN，同样得有人定义
    target_compile_definitions(DoodleGame PRIVATE VK_USE_PLATFORM_ANDROID_KHR)

    # libvulkan.so 由 NDK 的 sysroot 提供（Android 上 Vulkan 是系统库）。
    # android / log 是 NDK 的 platform 库
    target_link_libraries(DoodleGame PRIVATE
        vulkan
        android
        log
    )

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
# macOS（桌面）
# ============================================================================

# 第三方头文件 / 库的搜索目录。
# Vulkan SDK 在 /usr/local，Homebrew 的 GLFW / GLM 在 /opt/homebrew。
set(DOODLE_HEADER_SEARCH_PATHS
    /usr/local/include
    /opt/homebrew/include
    ${CMAKE_SOURCE_DIR}/Runtime
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

# 创建一个叫 DoodleGame 的可执行文件 target，并列出源文件。
# MACOSX_BUNDLE = 在 macOS 上打成 DoodleGame.app，而不是裸的可执行文件。
#   产物结构：DoodleGame.app/Contents/MacOS/DoodleGame
#             DoodleGame.app/Contents/Frameworks/*.dylib
#             DoodleGame.app/Contents/Info.plist（CMake 自动生成）
# .cpp 会真正编译；.h 写进来是让 CLion 显示在工程树里、改头文件时能触发重编。
# 路径相对本 CMakeLists.txt 所在目录。以后加新 .cpp，改的就是上面的清单变量。
add_executable(DoodleGame MACOSX_BUNDLE
    ${DOODLE_SHARED_SOURCES}
    ${DOODLE_MACOS_SOURCES}
)

# 把头文件搜索路径接到这个 target 上（只给 DoodleGame 自己用，所以是 PRIVATE）。
# 第三方头文件直接用上面的搜索目录，不再走 find_path；
# Runtime/ 也在那里面，所以 #include "Vulkan/xxx.h"、"Platform/xxx.h" 这类能解析
# （平台文件夹内部之间的引用走 "../xxx.h" 相对路径，不经过这条）。
# _doodle_shader_output_dir 是着色器内嵌副本的生成位置，见上面的 add_custom_command。
#
# 这里**不要**再放工程根目录（CMAKE_SOURCE_DIR 本身）。
# 曾经有一行，理由是「这样 #include "Runtime/xxx.h" 才能从根目录拼出来」，
# 但工程里根本没有任何一处这样写 —— 全是 "Vulkan/xxx.h" 或 "xxx.h" 这种形式。
# 它的实际效果是把根目录下所有东西都当成头文件搜索路径，
# Android Studio 的工程视图会把 .git、.idea、cmake-build-debug 这些
# 一并当成「构建相关目录」列进 includes 分组里，一屏全是噪声
target_include_directories(DoodleGame PRIVATE
    ${DOODLE_HEADER_SEARCH_PATHS}
    ${_doodle_shader_output_dir}
)

# 把找到的两个 dylib 链进可执行文件。
# 链接器会记下 GLFW 的绝对路径，以及 Vulkan 的 @rpath/libvulkan.1.dylib（运行时再按 rpath 找）。
target_link_libraries(DoodleGame PRIVATE
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

    # 把 Shaders/ 拷进 .app 的 Contents/Resources/，让包自带着色器字节码。
    # 运行时的查找顺序见 DoodleShaderModule::ReadFile：
    #   先按「进程当前工作目录」找（开发时命中源码目录里的最新产物），
    #   找不到再用 <可执行文件目录>/../Resources/Shaders/（Finder 双击时走这条）。
    # 之所以放在包内 Resources 而不是可执行文件旁边，是遵循 macOS 的包约定。
    # copy_directory 会把 .vert/.frag 源码一并拷进去，体积可忽略；
    # 若以后不想随包分发源码，改成只拷 *.spv 即可。
    add_custom_command(TARGET DoodleGame POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/Shaders"
            "$<TARGET_BUNDLE_CONTENT_DIR:DoodleGame>/Resources/Shaders"
        COMMENT "Copy Shaders into DoodleGame.app/Contents/Resources/Shaders"
    )
endif()
