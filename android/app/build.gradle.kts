plugins {
    id("com.android.application")
}

android {
    // 命名空间与 applicationId 保持一致。命名空间决定 R 类与 BuildConfig 的包名，
    // 本工程没有 Java 源码、用不到它们，但 AGP 要求必须有
    namespace = "com.doodle.doodlegame"
    compileSdk = 37

    // 钉死 NDK 版本。
    //
    // 不写这一行的话，AGP 会按它内置的「已知 NDK 版本」清单自己挑一个 ——
    // 实测它会忽略机器上已有的 r30，转头下载一份它默认要的 r28c。
    // 那样构建结果就取决于 AGP 版本，以后升级 AGP 可能悄悄换掉 NDK，
    // 表现为「什么都没改，行为却变了」。
    //
    // 钉死之后，这一版就是唯一会用的。机器上没装的话 AGP 会自己下
    ndkVersion = "30.0.16248370"

    defaultConfig {
        applicationId = "com.doodle.doodlegame"

        // 33 = Android 13。这个数字由**链接期**决定，不是「支持哪个 Vulkan 版本」那条线：
        // 系统 libvulkan.so 导出哪些函数，按 Android 版本逐级放开，而 NDK 链接时挑哪一份
        // stub 由 minSdk 定。实测各 API 级别的 vk 导出数：24 → 148、29 → 182、32 → 195、
        // 33 → 232 —— Vulkan 1.3 的入口点（vkCmdBeginRendering / vkCmdEndRendering /
        // vkQueueSubmit2 / vkCmdPipelineBarrier2）正是从 33 才开始出现。低于 33 会在链接期
        // 直接报 undefined symbol，APK 根本编不出来。选 33 而不是更高的值，是因为它就是
        // 满足需求的最低版本，再高只是白白缩小设备覆盖面
        //
        // 另一条线（运行期）与这个数字无关：DoodleVulkanDevice 请求 apiVersion 1.3，
        // 设备驱动不支持就会在 vkCreateInstance 失败。CDD 只强制手机支持 1.1（Android 15 起），
        // 1.3 到 Android 16 仍是「强烈推荐」—— 没有任何 minSdk 能保证它。现在收口到 33
        // 是因为只在测试机上跑；真要发布还得加运行期探测（vkEnumerateInstanceVersion，
        // 或读 VkPhysicalDeviceProperties::apiVersion），或者接受非 1.3 设备装不上
        minSdk = 33
        targetSdk = 37
        versionCode = 1
        versionName = "1.0"

        // 只出 arm64-v8a。绝大多数现役设备都是它，出别的 ABI 只会让 APK 变大。
        // 要用模拟器的话加 "x86_64"
        ndk {
            abiFilters += "arm64-v8a"
        }

        externalNativeBuild {
            cmake {
                // 不指定 ANDROID_STL，走默认的 c++_static —— STL 直接链进 libDoodleGame.so，
                // APK 里不必再带一个 libc++_shared.so，少一个可能版本对不上的动态库
            }
        }
    }

    externalNativeBuild {
        cmake {
            // 与桌面共用的那一份。它在仓库根目录，本模块在 android/app/ 下，
            // 所以要往上两级
            path = file("../../CMakeLists.txt")

            // 钉死 CMake 版本。SDK 里装了 3.22.1 和 4.1.2 两版，不写的话
            // AGP 会按自己的默认值挑，将来某个自动更新换了版本，
            // 构建行为会莫名其妙地变
            version = "3.22.1"
        }
    }

    // 运行时资产：把仓库根的 Resources/ 收作 assets 源目录。
    //
    // 这样 APK 里的路径与 Resources/ 下的相对路径一一对应，
    // 原生侧读 "Scenes/main.json" 就能命中 assets/Scenes/main.json，
    // 与 macOS 的 .app/Contents/Resources、Windows 的输出目录是同一种布局。
    //
    // 不往 src/main/assets 里拷贝：那会出现第二份内容，改了一处忘另一处是迟早的事。
    // Gradle 本来就支持模块目录之外的源目录；src/main/assets 仍按默认规则保留，
    // 以后真有 Android 专属资产时不必再改这段
    sourceSets {
        getByName("main") {
            assets.directories.add("../../Resources")
        }
    }

    buildTypes {
        release {
            // 本工程没有 Java 字节码要压缩，混淆器无从下手。
            // 原生库的剥离由 AGP 按 debugSymbolLevel 处理，与本开关无关
            isMinifyEnabled = false
        }
    }

    // 纯原生应用，没有 Java/Kotlin 源码目录，所以不配 java/kotlin 的 sourceSets；
    // 上面的 assets 源目录是唯一的例外
}

// 把 AGP 默认带进来的 kotlin-stdlib 剔掉。
//
// AGP 9 的内置 Kotlin 支持会默认往运行时类路径塞一份 kotlin-stdlib
// （实测 2.2.10，约 2.4MB）。本工程一行 Kotlin 都没有，而 AndroidManifest 里
// hasCode="false" 又意味着系统根本不加载 dex —— 那份 stdlib 是纯死重量，
// 占掉 APK 的一半。剔掉之后 2.0M → 1.2M。
//
// 为什么不用 android.builtInKotlin=false（效果相同）：AGP 已经提示那个选项弃用、
// AGP 10 将移除。依赖排除是普通 Gradle 机制，不会过期。
// 代价是以后真写 Kotlin 时要记得删掉这几行，否则会看到「找不到 kotlin 包」这种
// 与根因对不上的报错
configurations.configureEach {
    exclude(group = "org.jetbrains.kotlin", module = "kotlin-stdlib")
    exclude(group = "org.jetbrains", module = "annotations")
}

// 注：产物里仍有两个小 dex（classes.dex 是 AGP 生成的空 R 类；
// classes2.dex 约 129KB，是 AGP 9 自己塞进去的 android.jar 框架类桩）。
// 实测与 useAndroidX、与依赖声明都无关，是 AGP 行为，没有开关可关。
// hasCode="false" 下它们不会被加载，留着不影响运行
