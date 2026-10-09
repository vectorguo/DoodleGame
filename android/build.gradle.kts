// 根工程的构建脚本，只声明插件版本，不套用到任何模块上（apply false）。
//
// 版本选择：
//   AGP 9.4.1   —— 查 Google Maven 得到的最新稳定版
//   Gradle 9.8.1 —— wrapper 里指定，是当前版
//   JDK 25       —— 用 Android Studio 自带的 JBR，不需要另装
// 这三者要对得上：AGP 9.x 要求 Gradle 9.x。
// 改动前先去 https://dl.google.com/dl/android/maven2/com/android/tools/build/gradle/maven-metadata.xml
// 和 https://services.gradle.org/versions/current 对一下，别凭记忆写版本号
plugins {
    id("com.android.application") version "9.4.1" apply false
}
