# 从 Live2D 官方 SDK 包里取出 Windows 版 Cubism Core DLL，放到 lib/。
# 用法:
#   tools/fetch_live2d_core.ps1
#   tools/fetch_live2d_core.ps1 -SdkZip <本地 SDK.zip>
#   tools/fetch_live2d_core.ps1 -Version 5-r.5
#
# Core 是 Live2D 的专有二进制，不随本项目分发；下载即表示你接受 Live2D 的许可条款。
param(
    [string]$SdkZip = "",
    [string]$Version = "5-r.4"
)

$ErrorActionPreference = "Stop"
$Here = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$Tmp = Join-Path $env:TEMP ("fxpet-cubism-" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Force $Tmp | Out-Null

try {
    $Zip = Join-Path $Tmp "sdk.zip"
    if ($SdkZip) {
        Copy-Item -Force $SdkZip $Zip
    } else {
        $Url = "https://cubism.live2d.com/sdk-native/bin/CubismSdkForNative-$Version.zip"
        Write-Host "下载 $Url"
        Invoke-WebRequest -UseBasicParsing -Uri $Url -OutFile $Zip
    }

    $Entry = (tar -tf $Zip | Select-String -Pattern 'Core/dll/windows/x86_64/Live2DCubismCore\.dll$' |
        Select-Object -First 1).Line
    if (-not $Entry) {
        throw "该 SDK 包里没有 Windows x64 版 Core DLL"
    }

    tar -xf $Zip -C $Tmp $Entry
    $LibDir = Join-Path $Here "lib"
    New-Item -ItemType Directory -Force $LibDir | Out-Null
    Copy-Item -Force (Join-Path $Tmp $Entry) (Join-Path $LibDir "Live2DCubismCore.dll")
    Write-Host "已放置: lib\Live2DCubismCore.dll"
} finally {
    Remove-Item -Recurse -Force $Tmp -ErrorAction SilentlyContinue
}
