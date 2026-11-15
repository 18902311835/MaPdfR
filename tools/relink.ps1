# 把 MaPdfR 的应用程序目标文件与修改后的 Qt 重新链接。
#
# 这是 LGPL v3 第 4(d)(0) 条要求的“可重新链接”材料的一部分：
# 发行包的 source\relink\ 目录附带本脚本、应用程序的全部 .obj/.res、
# MuPDF 静态库，以及本次构建使用的链接库清单 link-libs.txt。
# Qt 静态库来自用户自己的 Qt 构建（通常是把本程序用到的 Qt 修改后重编）。
#
# 用法（在 source\relink 目录）：
#   relink.bat <Qt 前缀>
# 或：
#   powershell -ExecutionPolicy Bypass -File relink.ps1 -QtPrefix E:\my-qt-build
#
# 可选参数 -VcVars 指定 vcvars64.bat；-OutExe 指定输出文件名。

param(
    [string]$QtPrefix = $env:QT_PREFIX,
    [string]$VcVars = $env:VCVARS,
    [string]$OutExe = ""
)

$ErrorActionPreference = "Stop"

$Here = $PSScriptRoot

if (-not $QtPrefix) {
    $QtPrefix = "E:\QT\6.11.1\msvc2022_64-static"
}
if (-not $VcVars) {
    $VcVars = "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat"
}

if (-not (Test-Path (Join-Path $QtPrefix "lib\Qt6Core.lib"))) {
    throw "Qt 静态库不存在：$QtPrefix\lib\Qt6Core.lib。请用 -QtPrefix 指向修改后重新编译的 Qt。"
}
if (-not (Test-Path $VcVars)) {
    throw "找不到 vcvars64.bat：$VcVars（可用 -VcVars 指定）。"
}

$objDir = Join-Path $Here "obj"
if (-not (Test-Path $objDir)) {
    throw "缺少目标文件目录：$objDir"
}

$objects = Get-ChildItem $objDir -Recurse -File |
    Where-Object { $_.Extension -in ".obj", ".res" } |
    Sort-Object FullName
if (-not $objects) {
    throw "目标文件目录为空：$objDir"
}

$libsFile = Join-Path $Here "link-libs.txt"
if (-not (Test-Path $libsFile)) {
    throw "缺少链接库清单：$libsFile"
}

$qtMarker = "@QT_PREFIX@\"
$resolvedLibs = foreach ($entry in (Get-Content $libsFile)) {
    $lib = $entry.Trim()
    if (-not $lib) {
        continue
    }
    if ($lib.StartsWith($qtMarker)) {
        Join-Path $QtPrefix $lib.Substring($qtMarker.Length)
    } elseif (Test-Path (Join-Path $Here $lib)) {
        Join-Path $Here $lib
    } else {
        $lib
    }
}

$rsp = Join-Path ([System.IO.Path]::GetTempPath()) "mapdfr-relink.rsp"
$lines = @()
foreach ($item in $objects) {
    $lines += '"' + $item.FullName + '"'
}
foreach ($lib in $resolvedLibs) {
    $lines += '"' + $lib + '"'
}
Set-Content -Path $rsp -Value $lines -Encoding ASCII

if (-not $OutExe) {
    $OutExe = Join-Path $Here "MaPdfR-relinked.exe"
}

Write-Host "[INFO] Qt     : $QtPrefix"
Write-Host "[INFO] Object : $objDir ($($objects.Count) files)"
Write-Host "[INFO] Output : $OutExe"

Write-Host "[INFO] Importing MSVC environment and linking ..."
$link = "link.exe /NOLOGO /OUT:`"$OutExe`" /SUBSYSTEM:WINDOWS /MACHINE:X64 " +
        "/INCREMENTAL:NO /MANIFEST:EMBED `"@$rsp`""
$command = "`"$VcVars`" >nul && $link"
cmd.exe /c $command
if ($LASTEXITCODE -ne 0) {
    throw "重新链接失败（link.exe 退出码 $LASTEXITCODE）。"
}

Write-Host "[DONE] $OutExe"
Write-Host "注意：relink 只替换 Qt 库；若 Qt 的修改引入了新的插件或库，请同步更新 link-libs.txt。"
