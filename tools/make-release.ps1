# MaPdfR 发行包打包脚本（由 tools\make-release.bat 调用）。
#
# 输出 dist\MaPdfR-<version>\：
#   MaPdfR.exe            程序本体
#   pdf.ico / app.ico     文件类型图标（注册表 DefaultIcon 引用 pdf.ico）
#   README.md PROFESSIONAL_REVIEW.md THIRD_PARTY.md COPYRIGHT.md LICENSE.txt user-guide.zh_CN.html
#   SOURCE-INFO.txt       源码与许可说明
#   SHA256SUMS.txt        逐文件校验值
#   source\               附带源码（AGPL / LGPL 履约材料）
#     MaPdfR-<version>-source.zip   本工程源码、构建脚本与测试
#     MuPDF-1.27.2-source.zip       MuPDF 及其 thirdparty 依赖源码
#
# 用法：tools\make-release.ps1 [-ExePath <exe>] [-MuPdfDir <dir>] [-SkipMuPdfSource]

param(
    [string]$ExePath = "",
    [string]$MuPdfDir = $env:MUPDF_DIR,
    [switch]$SkipMuPdfSource
)

$ErrorActionPreference = "Stop"

$ProjectDir = Split-Path -Parent $PSScriptRoot
$BuildDir = Join-Path $ProjectDir "build\msvc2022_64-static"

if (-not $ExePath) {
    $ExePath = Join-Path $BuildDir "MaPdfR.exe"
}
if (-not (Test-Path $ExePath)) {
    throw "找不到 MaPdfR.exe：$ExePath`n请先运行 bld.bat，或用 -ExePath 指定路径。"
}

$cmake = Get-Content (Join-Path $ProjectDir "CMakeLists.txt") -Raw
if ($cmake -match 'project\s*\(\s*MaPdfR\s+VERSION\s+([0-9][0-9.]*)') {
    $Version = $Matches[1]
} else {
    throw "无法从 CMakeLists.txt 读取版本号。"
}

$DistRoot = Join-Path $ProjectDir "dist"
$Dist = Join-Path $DistRoot "MaPdfR-$Version"
$SourceDir = Join-Path $Dist "source"

Write-Host "[INFO] Version : $Version"
Write-Host "[INFO] Exe     : $ExePath"
Write-Host "[INFO] Output  : $Dist"

if (Test-Path $Dist) {
    Remove-Item -Recurse -Force $Dist
}
New-Item -ItemType Directory -Path $SourceDir -Force | Out-Null

# --- 程序与文档 ---
Copy-Item $ExePath $Dist
Copy-Item (Join-Path $ProjectDir "resources\help\user-guide.zh_CN.html") $Dist
Copy-Item (Join-Path $ProjectDir "licenses") $Dist -Recurse
foreach ($name in @("README.md", "PROFESSIONAL_REVIEW.md", "THIRD_PARTY.md", "COPYRIGHT.md", "LICENSE.txt")) {
    $path = Join-Path $ProjectDir $name
    if (Test-Path $path) {
        Copy-Item $path $Dist
    }
}
foreach ($name in @("pdf.ico", "app.ico")) {
    $path = Join-Path $ProjectDir "resources\$name"
    if (Test-Path $path) {
        Copy-Item $path $Dist
    }
}

# --- 本工程源码包 ---
$sourceItems = @(
    "src", "cmake", "resources", "tests", "tools", "licenses",
    "CMakeLists.txt", "bld.bat",
    "README.md", "PROFESSIONAL_REVIEW.md", "THIRD_PARTY.md", "COPYRIGHT.md", "LICENSE.txt"
) | ForEach-Object { Join-Path $ProjectDir $_ } |
    Where-Object { Test-Path $_ }

$projectZip = Join-Path $SourceDir "MaPdfR-$Version-source.zip"
Write-Host "[INFO] Packaging project source ..."
Compress-Archive -Path $sourceItems -DestinationPath $projectZip -CompressionLevel Optimal

# --- MuPDF 源码包（AGPL 履行材料）---
$mupdfSource = $null
if ($MuPdfDir) {
    $candidate = Join-Path $MuPdfDir "source\fitz\noto.c"
    if (Test-Path $candidate) {
        $mupdfSource = $MuPdfDir
    }
}

if ($mupdfSource -and -not $SkipMuPdfSource) {
    $mupdfZip = Join-Path $SourceDir "MuPDF-1.27.2-source.zip"
    Write-Host "[INFO] Packaging MuPDF source ($mupdfSource) ..."

    # 源码树里常有 VS 的中间产物（platform\win32\x64、Release 等，动辄数 GB），
    # 先复制到临时目录并排除构建输出，再压缩，避免把几 GB 的 .obj 打进源码包。
    $stage = Join-Path ([System.IO.Path]::GetTempPath()) "mapdfr-mupdf-src"
    $stageTree = Join-Path $stage "mupdf-1.27.2-source"
    if (Test-Path $stage) {
        Remove-Item -Recurse -Force $stage
    }
    New-Item -ItemType Directory -Path $stage -Force | Out-Null

    robocopy $mupdfSource $stageTree /E /NFL /NDL /NJH /NJS /NP `
        /XD ".git" "x64" "Release" "Releasex64" "Debug" `
        /XF "*.user" "*.obj" "*.lib" "*.pdb" "*.ilk" "*.idb" | Out-Null

    if ($LASTEXITCODE -ge 8) {
        throw "复制 MuPDF 源码失败（robocopy 退出码 $LASTEXITCODE）。"
    }

    Compress-Archive -Path $stageTree -DestinationPath $mupdfZip -CompressionLevel Optimal
    Remove-Item -Recurse -Force $stage
} elseif (-not $SkipMuPdfSource) {
    Write-Warning ("未找到 MuPDF 源码树（MUPDF_DIR={0}）。" -f $MuPdfDir)
    Write-Warning "发行含 MuPDF 的二进制时，必须同时提供其完整源码。"
    Write-Warning "设置 MUPDF_DIR 后重新运行，或把官方的 mupdf-1.27.2-source.zip 放进 source\。"
}

# --- 源码与许可说明 ---
$sourceInfo = @"
MaPdfR $Version 发行包 · 源码与第三方许可说明

本工程以 AGPL v3 发布。以下材料用于核对本次发行的对应源码。
静态 Qt 构建的重链接脚本随本工程源码提供。正式发行前须按实际许可证补齐并验证重链接材料。

  source\MaPdfR-$Version-source.zip
      本工程全部源码：src\、cmake\、resources\、tests\、tools\、
      CMakeLists.txt、bld.bat，以及 README / THIRD_PARTY / LICENSE。

  source\MuPDF-1.27.2-source.zip
      MuPDF 1.27.2 完整源码（含 thirdparty\ 下的 freetype、zlib、
      libjpeg、openjpeg、jbig2dec、lcms2、brotli 等依赖）。
      若此文件缺失，请从 https://github.com/ArtifexSoftware/mupdf
      获取 1.27.2 源码，或设置 MUPDF_DIR 后重新运行 tools\make-release.bat。

其余组件的源码 / 许可文本位置：
  Qt 6.11.1      LGPL v3 或 Qt 商业许可   https://www.qt.io
  PDFium         BSD-3-Clause             https://pdfium.googlesource.com/pdfium/
  qtbase_zh_CN.qm    随 Qt 分发（LGPL v3）      Qt 安装目录 translations\

逐项依赖、版本与许可见 THIRD_PARTY.md。

重新构建：
  1. 解压 MaPdfR-$Version-source.zip；
  2. 准备 Visual Studio 2022 x64、CMake、Ninja、静态 Qt 6（含 Qt PDF），
     以及 MuPDF 1.27.2 源码树；
  3. 设置 QT_PREFIX / MUPDF_DIR，运行 bld.bat。

本文件与 SHA256SUMS.txt 由 tools\make-release.bat 生成。
"@
Set-Content -Path (Join-Path $Dist "SOURCE-INFO.txt") -Value $sourceInfo -Encoding UTF8

# --- 校验值 ---
Write-Host "[INFO] Writing SHA256SUMS.txt ..."
$sums = Get-ChildItem $Dist -Recurse -File |
    Where-Object { $_.Name -ne "SHA256SUMS.txt" } |
    ForEach-Object {
        $hash = (Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        $relative = $_.FullName.Substring($Dist.Length + 1).Replace('\', '/')
        "$hash  $relative"
    }
Set-Content -Path (Join-Path $Dist "SHA256SUMS.txt") -Value $sums -Encoding ASCII

Write-Host ""
Write-Host "[DONE] Release folder: $Dist"
Get-ChildItem $Dist | Select-Object Name, Length | Format-Table -AutoSize
Get-ChildItem $SourceDir | Select-Object Name, @{N='MB';E={[math]::Round($_.Length / 1MB, 1)}} |
    Format-Table -AutoSize
