#!/usr/bin/env bash
#
# 构建 → 安装 → 启动 → 跟随日志，一条命令走完。
#
# 用法：
#   ./run.sh                构建 + 安装 + 启动 + 跟随日志（Ctrl-C 退出，不会停掉应用）
#   ./run.sh --no-log       同上，但启动完就返回
#   ./run.sh --build-only   只构建，不碰设备
#
# 没有走 Android Studio 的 Run 按钮，是因为这里的每一步都能看见、能单独拿出来用 ——
# 出问题时容易定位是构建、安装还是启动哪一环。AS 那边按 Run 更省事，
# 两套并存，按场合挑。

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

# ---- 参数 ----

WITH_LOG=1
BUILD_ONLY=0
for arg in "$@"; do
    case "$arg" in
        --no-log)     WITH_LOG=0 ;;
        --build-only) BUILD_ONLY=1 ;;
        -h|--help)    sed -n '3,12p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *)            echo "未知参数：$arg（用 --help 看用法）" >&2; exit 2 ;;
    esac
done

# ---- JDK ----
#
# 本机没有系统 Java，命令行跑 gradlew 会在起 JVM 那一步就失败，
# 报的还是「Unable to locate a Java Runtime」这种看不出跟 Android 有关的错。
# ~/.zshrc 里已经 export 了 JAVA_HOME，这里再兜一层 ——
# 脚本可能被从 cron、IDE、或者一个没加载 .zshrc 的 shell 里调起来

if [ -z "${JAVA_HOME:-}" ]; then
    JAVA_HOME="$HOME/Applications/Android Studio.app/Contents/jbr/Contents/Home"
    export JAVA_HOME
fi
if [ ! -x "$JAVA_HOME/bin/java" ]; then
    echo "找不到 JDK：$JAVA_HOME" >&2
    echo "装 Android Studio 会自带 JBR。若它装在别处，改本脚本里的这个默认值。" >&2
    exit 1
fi

# ---- 构建 ----

echo "==> 构建 debug APK"
./gradlew assembleDebug --console=plain

APK="app/build/outputs/apk/debug/app-debug.apk"
if [ ! -f "$APK" ]; then
    echo "构建完了却没找到 $APK" >&2
    exit 1
fi
echo "    $(ls -lh "$APK" | awk '{print $5}')  $APK"

if [ "$BUILD_ONLY" -eq 1 ]; then
    exit 0
fi

# ---- 找 adb ----
#
# 优先用 ANDROID_HOME（~/.zshrc 里设了），退到 local.properties 里那个 sdk.dir，
# 再退到默认安装位置。都不行就报清楚

ADB=""
for candidate in \
    "${ANDROID_HOME:-}/platform-tools/adb" \
    "$(sed -n 's/^sdk\.dir=//p' local.properties 2>/dev/null | head -1)/platform-tools/adb" \
    "$HOME/Library/Android/sdk/platform-tools/adb"
do
    if [ -n "$candidate" ] && [ -x "$candidate" ]; then
        ADB="$candidate"
        break
    fi
done

if [ -z "$ADB" ]; then
    echo "找不到 adb。确认 SDK 装好了，或把路径写进 android/local.properties 的 sdk.dir" >&2
    exit 1
fi

# ---- 确认有设备 ----
#
# 不这么做的话，后面的 adb 会报「no devices/emulators found」，
# 那种信息量太低的错不值得让人在这上面花时间

DEVICE_COUNT=$("$ADB" devices | grep -c "[[:space:]]device$" || true)
if [ "$DEVICE_COUNT" -eq 0 ]; then
    echo "没有连接的设备。" >&2
    echo "插上 USB、在手机上打开「开发者选项 → USB 调试」，然后确认：" >&2
    echo "  \"$ADB\" devices" >&2
    exit 1
fi
if [ "$DEVICE_COUNT" -gt 1 ]; then
    echo "连了 $DEVICE_COUNT 台设备，adb 会自己挑一台。" >&2
    echo "要指定的话加环境变量 ANDROID_SERIAL=<序列号> 再跑一遍。" >&2
fi

# ---- 应用标识 ----
#
# 从 build.gradle.kts 里读，不在这里再写一份 —— 写两份早晚会不一致。
# 权威值是构建产物里的那个，但为读它要多引一个 aapt2，不划算

APP_ID=$(sed -n 's/^[[:space:]]*applicationId[[:space:]]*=[[:space:]]*"\(.*\)".*/\1/p' \
         app/build.gradle.kts | head -1)
if [ -z "$APP_ID" ]; then
    echo "没能从 app/build.gradle.kts 里读出 applicationId" >&2
    exit 1
fi
ACTIVITY="android.app.NativeActivity"

# ---- 安装 ----

echo "==> 安装到设备"
# -r 覆盖安装（保留应用数据）；-d 允许版本号降级，调试时改了 versionCode 往回退要用
"$ADB" install -r -d "$APK"

# ---- 启动 ----

echo "==> 启动 $APP_ID/$ACTIVITY"
"$ADB" shell am start -n "$APP_ID/$ACTIVITY" > /dev/null

if [ "$WITH_LOG" -eq 0 ]; then
    exit 0
fi

# ---- 跟随日志 ----

echo "==> 日志（Ctrl-C 退出；只显示此刻之后的内容）"
echo

# -T <时间戳> 表示「只显示这个时刻之后的日志」。
# 用它而不是 adb logcat -c，是因为 -c 会把整个设备的日志缓冲区清空，
# 那会连带抹掉你同时在调的其他东西。这里既只看得到本次启动的输出，
# 也不动别人的数据
#
# 过滤只要的那几个 tag：
#   Doodle          = 本工程的日志出口（见 Runtime/Platform/DoodleLog.cpp）
#   AndroidRuntime  = 未捕获异常与崩溃
#   DEBUG           = 原生崩溃的 tombstone 摘要
#   *:S             = 其余一律静音
exec "$ADB" logcat -T "$(date '+%m-%d %H:%M:%S.000')" \
    Doodle:V AndroidRuntime:E DEBUG:E "*:S"
