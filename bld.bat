@echo off
setlocal enabledelayedexpansion

REM ============================================================
REM  MaPdfR build script (Qt 6.11.1 static, MSVC 2022 x64, Ninja)
REM
REM  Usage:
REM    bld.bat          configure + build + ctest
REM    bld.bat clean    delete the build directory and rebuild
REM    bld.bat run      build, then launch MaPdfR.exe
REM
REM  Environment overrides:
REM    QT_PREFIX   Qt kit prefix (default E:\QT\6.11.1\msvc2022_64-static)
REM    MUPDF_DIR   MuPDF 1.27 source tree (default E:\third_party\mupdf-1.27.2-source)
REM    VCVARS      vcvars64.bat path
REM    CMAKE_BIN   directory containing cmake.exe
REM    NINJA_BIN   directory containing ninja.exe
REM ============================================================

set "PROJECT_DIR=%~dp0"
if not defined VCVARS set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat"
if not defined QT_PREFIX set "QT_PREFIX=E:\QT\6.11.1\msvc2022_64-static"
if not defined MUPDF_DIR set "MUPDF_DIR=E:\third_party\mupdf-1.27.2-source"
if not defined CMAKE_BIN set "CMAKE_BIN=E:\QT\Tools\CMake_64\bin"
if not defined NINJA_BIN set "NINJA_BIN=E:\QT\Tools\Ninja"

set "DO_CLEAN="
set "DO_RUN="
if /i "%~1"=="clean" set "DO_CLEAN=1"
if /i "%~1"=="run" set "DO_RUN=1"

set "BUILD_DIR=%PROJECT_DIR%build\msvc2022_64-static"
set "JOBS=%NUMBER_OF_PROCESSORS%"

echo ============================================================
echo   MaPdfR static build
echo   Project: %PROJECT_DIR%
echo   Qt kit:  %QT_PREFIX%
echo   MuPDF:   %MUPDF_DIR%
echo   Build:   %BUILD_DIR%
echo   Jobs:    %JOBS%
echo ============================================================

if not exist "%VCVARS%" (
    echo [ERROR] vcvars64.bat not found: "%VCVARS%"
    exit /b 1
)
if not exist "%QT_PREFIX%\lib\cmake\Qt6\Qt6Config.cmake" (
    echo [ERROR] Qt6 not found: "%QT_PREFIX%"
    exit /b 1
)
if not exist "%QT_PREFIX%\lib\cmake\Qt6PdfWidgets\Qt6PdfWidgetsConfig.cmake" (
    echo [ERROR] Qt6 PDF Widgets module missing in: "%QT_PREFIX%"
    exit /b 1
)
if not exist "%CMAKE_BIN%\cmake.exe" (
    echo [ERROR] cmake.exe not found: "%CMAKE_BIN%"
    exit /b 1
)
if not exist "%NINJA_BIN%\ninja.exe" (
    echo [ERROR] ninja.exe not found: "%NINJA_BIN%"
    exit /b 1
)
if not exist "%MUPDF_DIR%\platform\win32\libmupdf.vcxproj" (
    echo [ERROR] MuPDF source not found: "%MUPDF_DIR%"
    echo         Set MUPDF_DIR to the MuPDF 1.27.x source tree.
    exit /b 1
)

echo [INFO] Initializing VS2022 x64 environment...
call "%VCVARS%" >nul
if errorlevel 1 (
    echo [ERROR] vcvars64.bat failed
    exit /b 1
)
if /i not "%VSCMD_ARG_TGT_ARCH%"=="x64" (
    echo [ERROR] Target arch is "%VSCMD_ARG_TGT_ARCH%", expected x64
    exit /b 1
)

set "PATH=%CMAKE_BIN%;%NINJA_BIN%;%PATH%"

if defined DO_CLEAN (
    if exist "%BUILD_DIR%" (
        echo [INFO] Removing build directory...
        rmdir /s /q "%BUILD_DIR%"
    )
)

echo [INFO] Configure...
cmake -S "%PROJECT_DIR%." -B "%BUILD_DIR%" -G Ninja -Wno-dev ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_PREFIX_PATH="%QT_PREFIX%" ^
    -DMUPDF_SOURCE_DIR="%MUPDF_DIR%" ^
    -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded
if errorlevel 1 (
    echo [ERROR] configure failed
    exit /b 1
)

echo [INFO] Build with %JOBS% jobs...
cmake --build "%BUILD_DIR%" --parallel %JOBS%
if errorlevel 1 (
    echo [ERROR] build failed
    exit /b 1
)

echo [INFO] Running tests...
ctest --test-dir "%BUILD_DIR%" --output-on-failure
if errorlevel 1 (
    echo [ERROR] tests failed
    exit /b 1
)

echo.
echo ============================================================
echo [DONE] MaPdfR.exe:
echo        %BUILD_DIR%\MaPdfR.exe
echo ============================================================

if defined DO_RUN (
    start "" "%BUILD_DIR%\MaPdfR.exe"
)

endlocal
exit /b 0
