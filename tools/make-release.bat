@echo off
REM ============================================================
REM  MaPdfR release packager
REM
REM  Usage:
REM    tools\make-release.bat                 runtime package (EXE + documents, no source)
REM    tools\make-release.bat -WithSource     also build source zips in dist\
REM    tools\make-release.bat -WithSource -SkipMuPdfSource
REM
REM  Environment:
REM    MUPDF_DIR  MuPDF 1.27.x source tree (default E:\third_party\mupdf-1.27.2-source)
REM
REM  Output: dist\MaPdfR-<version>\ (runtime, no source)
REM          dist\*-source.zip      (only with -WithSource)
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
