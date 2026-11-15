@echo off
REM ============================================================
REM  用修改后的静态 Qt 重新链接 MaPdfR（LGPL v3 §4(d)(0)）
REM
REM  用法（在 source\relink 目录）：
REM    relink.bat "E:\my-qt-build"
REM
REM  环境变量：
REM    VCVARS  vcvars64.bat 路径
REM ============================================================
setlocal
if not defined VCVARS set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat"

powershell -NoProfile -ExecutionPolicy Bypass ^
    -File "%~dp0relink.ps1" -QtPrefix "%~1" -VcVars "%VCVARS%"

if errorlevel 1 (
    echo [ERROR] relink failed
    exit /b 1
)

endlocal
exit /b 0
