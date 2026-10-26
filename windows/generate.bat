@echo off
rem ============================================================================
rem Generate the Visual Studio solution for Windows (DoodleGame.sln) and open it.
rem
rem Usage:
rem   generate.bat              generate, then open the .sln (what a double-click does)
rem   generate.bat --no-open    generate only
rem
rem Output goes to windows\build\. The whole directory is generated and not tracked
rem by git; delete it and run this script again to start over. The real project is
rem the CMakeLists.txt at the repository root. After editing it there is no need to
rem rerun this script: building in VS runs ZERO_CHECK first, which regenerates.
rem
rem This script only finds Visual Studio and CMake and fills in the generator.
rem Manual equivalent, from a VS "Developer Command Prompt" at the repository root:
rem   cmake -S . -B windows\build -G "Visual Studio 17 2022" -A x64
rem Command-line build:
rem   cmake --build windows\build --config Debug
rem
rem Prerequisites:
rem   - Visual Studio 2022 or newer with the "Desktop development with C++" workload
rem     (its "C++ CMake tools for Windows" component ships the CMake used here)
rem   - Vulkan SDK (https://vulkan.lunarg.com); its installer sets VULKAN_SDK
rem   - Git for Windows: Shaders\compile.sh runs under its bash
rem
rem This file is ASCII only, on purpose. cmd.exe misreads batch files that contain
rem multi-byte UTF-8 text: on some code pages and invocations it advances the file
rem offset by the wrong amount and runs the tail of a comment line as a command
rem (observed on a Chinese-locale console). The Chinese notes live in CMakeLists.txt.
rem ============================================================================

setlocal EnableExtensions

rem ---- arguments ----

set "DOODLE_OPEN_SOLUTION=1"
if "%~1"=="" goto :arguments_done
if /i "%~1"=="--no-open" (
    set "DOODLE_OPEN_SOLUTION=0"
    goto :arguments_done
)
echo Unknown argument: %~1  [the only option is --no-open]
goto :fail
:arguments_done

rem Repository root = parent of this script's directory; %%~f normalizes the ..\
for %%d in ("%~dp0..") do set "DOODLE_SOURCE_DIR=%%~fd"
set "DOODLE_BUILD_DIR=%DOODLE_SOURCE_DIR%\windows\build"

rem ---- Visual Studio ----
rem
rem vswhere ships with the VS Installer at a fixed location. Put its directory on
rem PATH and call it by name: a quoted full path inside for /f breaks, because the
rem parentheses in "(x86)" make cmd strip the quotes and split at the space.

set "PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer;%PATH%"
where vswhere >nul 2>nul
if errorlevel 1 (
    echo vswhere.exe not found - Visual Studio does not seem to be installed.
    echo Install Visual Studio 2022 or newer with the "Desktop development with C++" workload.
    goto :fail
)

rem Newest instance that has the MSVC x64 toolset. No -products: by default vswhere
rem only considers Community / Professional / Enterprise, so it never picks a Build
rem Tools instance, which has no IDE to open the .sln with.
set "DOODLE_VSWHERE_SELECT=-latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64"
set "DOODLE_VS_DIR="
for /f "usebackq delims=" %%i in (`vswhere %DOODLE_VSWHERE_SELECT% -property installationPath`) do set "DOODLE_VS_DIR=%%i"
if not defined DOODLE_VS_DIR (
    echo Visual Studio is installed, but no instance has the MSVC toolset.
    echo Add the "Desktop development with C++" workload in the Visual Studio Installer.
    goto :fail
)

rem Generator name is "Visual Studio <major> <year>", e.g. "Visual Studio 17 2022".
rem Both parts come from vswhere, so whatever version is installed is what we target.
for /f "usebackq tokens=1 delims=." %%i in (`vswhere %DOODLE_VSWHERE_SELECT% -property installationVersion`) do set "DOODLE_VS_MAJOR=%%i"
for /f "usebackq delims=" %%i in (`vswhere %DOODLE_VSWHERE_SELECT% -property catalog_productLineVersion`) do set "DOODLE_VS_YEAR=%%i"
set "DOODLE_GENERATOR=Visual Studio %DOODLE_VS_MAJOR% %DOODLE_VS_YEAR%"

rem ---- CMake ----
rem
rem Prefer the copy bundled with that VS: it always knows that VS's generator.
rem A CMake on PATH may be older than the IDE and fail with
rem "Could not create named generator".

set "DOODLE_CMAKE=%DOODLE_VS_DIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if exist "%DOODLE_CMAKE%" goto :cmake_found
set "DOODLE_CMAKE="
for /f "delims=" %%i in ('where cmake 2^>nul') do if not defined DOODLE_CMAKE set "DOODLE_CMAKE=%%i"
if defined DOODLE_CMAKE goto :cmake_found
echo CMake not found.
echo In the Visual Studio Installer, enable "C++ CMake tools for Windows" under
echo "Desktop development with C++", or install CMake 3.22+ and put it on PATH.
goto :fail
:cmake_found

rem ---- generate ----

echo ==^> CMake     : %DOODLE_CMAKE%
echo ==^> Generator : %DOODLE_GENERATOR%
echo ==^> Output    : %DOODLE_BUILD_DIR%
echo.

"%DOODLE_CMAKE%" -S "%DOODLE_SOURCE_DIR%" -B "%DOODLE_BUILD_DIR%" -G "%DOODLE_GENERATOR%" -A x64
if errorlevel 1 (
    echo.
    echo Generation failed - see the CMake output above.
    echo If it complains that the generator differs from a previous run, which happens
    echo after switching VS versions, delete windows\build and run this script again.
    goto :fail
)

set "DOODLE_SOLUTION=%DOODLE_BUILD_DIR%\DoodleGame.sln"
echo.
echo ==^> Generated: %DOODLE_SOLUTION%

if "%DOODLE_OPEN_SOLUTION%"=="1" start "" "%DOODLE_SOLUTION%"
endlocal
exit /b 0

:fail
echo.
rem A double-clicked console closes as soon as the script ends; wait so the error stays readable
pause
endlocal
exit /b 1
