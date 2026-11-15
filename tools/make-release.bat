@echo off
REM ============================================================
REM  MaPdfR release packager
REM
REM  Usage:
REM    tools\make-release.bat              build output + documents + source
REM    tools\make-release.bat /skip-mupdf  skip the MuPDF source archive
REM
REM  Environment:
REM    MUPDF_DIR  MuPDF 1.27.x source tree (default E:\third_party\mupdf-1.27.2-source)
REM
REM  Output: dist\MaPdfR-<version>\
REM ============================================================
setlocal

if not defined MUPDF_DIR set "MUPDF_DIR=E:\third_party\mupdf-1.27.2-source"

powershell -NoProfile -ExecutionPolicy Bypass ^
    -File "%~dp0make-release.ps1" %*

if errorlevel 1 (
    echo [ERROR] release packaging failed
    exit /b 1
)

endlocal
exit /b 0
