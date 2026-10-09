// Android 侧 Gradle 工程的设置文件。
//
// 工程放在 android/ 子目录而不是仓库根目录，是为了让桌面构建的根目录保持原样 ——
// 那边已经有一份 CMakeLists.txt 和 Runtime/、Shaders/，再混进 settings.gradle、
// gradlew、app/ 这些只会让「这是哪个平台的工程」变得不清楚。
// 两边共用同一份 CMakeLists.txt，路径见 app/build.gradle.kts 里的 externalNativeBuild

pluginManagement {
    repositories {
        google()
        mavenCentral()
        gradlePluginPortal()
    }
}

dependencyResolutionManagement {
    repositories {
        google()
        mavenCentral()
    }
}

rootProject.name = "DoodleGame"
include(":app")
