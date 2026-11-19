# Copyright (c) 2024-2026 Tuomas Terho
# SPDX-License-Identifier: MIT
#
# Set up the build environment in a PowerShell session:
#   . .\sw\tools\env.ps1
# Then, from the repository root:
#   west build -b christmas_star sw -d build/star
#   west flash -d build/star

$repo = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$venv = Join-Path $repo ".venv\Scripts"
$sdk = Join-Path $env:USERPROFILE "zephyr-sdk-1.0.1"
$ninja = Get-ChildItem "$env:LOCALAPPDATA\Microsoft\WinGet\Packages" -Recurse -Filter ninja.exe -ErrorAction SilentlyContinue |
    Select-Object -First 1
$jlink = Get-ChildItem "C:\Program Files\SEGGER" -Directory -Filter "JLink*" -ErrorAction SilentlyContinue |
    Sort-Object Name -Descending | Select-Object -First 1

$paths = @($venv, "C:\Program Files\CMake\bin")
if ($ninja) { $paths += $ninja.DirectoryName }
if ($jlink) { $paths += $jlink.FullName }
$env:PATH = ($paths -join ";") + ";" + $env:PATH
$env:ZEPHYR_SDK_INSTALL_DIR = $sdk
$env:ZEPHYR_TOOLCHAIN_VARIANT = "zephyr"

Write-Host "Zephyr environment ready (SDK: $sdk$(if ($jlink) { ", J-Link: $($jlink.Name)" }))"
