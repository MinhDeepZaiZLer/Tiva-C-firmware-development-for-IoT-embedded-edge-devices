# Merge bootloader.bin + project0.bin into ONE flashable image.
#
# Usage:
#   powershell -File merge_bins.ps1 [-Boot <path>] [-App <path>] [-Out <path>]
#
# Flash layout (see Bootloader/boot_flash.h):
#   Bootloader : 0x00000000 - 0x00003FFF
#   Application: 0x00004000 - ...
param(
    [string]$Boot = "$PSScriptRoot\Bootloader\bootloader.bin",
    [string]$App = "$PSScriptRoot\Debug\project0.bin",
    [string]$Out = "$PSScriptRoot\Debug\project0_merged.bin",
    [int]$AppOffset = 0x4000
)

$ErrorActionPreference = "Stop"

$bootBytes = [System.IO.File]::ReadAllBytes($Boot)
$appBytes = [System.IO.File]::ReadAllBytes($App)

if ($bootBytes.Length -gt $AppOffset) {
    throw "Bootloader ($($bootBytes.Length) bytes) exceeds region size ($AppOffset bytes)"
}

$merged = New-Object byte[] ($AppOffset + $appBytes.Length)
[Array]::Copy($bootBytes, 0, $merged, 0, $bootBytes.Length)
[Array]::Copy($appBytes, 0, $merged, $AppOffset, $appBytes.Length)

[System.IO.File]::WriteAllBytes($Out, $merged)

Write-Host "Merged image written: $Out"
Write-Host ("  boot {0} bytes @ 0x{1:X8}" -f $bootBytes.Length, 0)
Write-Host ("  app  {0} bytes @ 0x{1:X8}" -f $appBytes.Length, $AppOffset)
Write-Host ("  total {0} bytes" -f $merged.Length)

# Sanity check: initial SP word from app vector table must sit inside SRAM.
$sp = [BitConverter]::ToUInt32($merged, $AppOffset)
if (($sp -lt 0x20000000) -or ($sp -ge 0x20008000)) {
    throw "Sanity check failed: app vector SP = 0x$($sp.ToString('X8')) not in SRAM range"
}
Write-Host ("  sanity ok: app initial SP = 0x{0:X8}" -f $sp)
