<#
.SYNOPSIS
    Summarize a RAGE install: folders, file types, counts and sizes.

.DESCRIPTION
    Reads only file names and sizes. Nothing is copied out of the game folder,
    so the output is safe to paste into research notes.

.EXAMPLE
    .\tools\inventory.ps1 -GamePath "E:\SteamLibrary\steamapps\common\RAGE"
#>
param(
    [Parameter(Mandatory)][string]$GamePath
)

$root = (Resolve-Path $GamePath).Path.TrimEnd('\')
$files = Get-ChildItem $root -Recurse -File

function Format-MB($bytes) { '{0:N1}' -f ($bytes / 1MB) }

"# Inventory of $root"
""
"Total: $($files.Count) files, $(Format-MB ($files | Measure-Object Length -Sum).Sum) MB"
""
"## Folders"
""
"| Folder | Files | MB |"
"| --- | ---: | ---: |"
$files | Group-Object DirectoryName | Sort-Object Name | ForEach-Object {
    $rel = $_.Name.Substring($root.Length)
    if (-not $rel) { $rel = '\' }
    "| ``$rel`` | $($_.Count) | $(Format-MB ($_.Group | Measure-Object Length -Sum).Sum) |"
}
""
"## File types"
""
"| Extension | Files | MB |"
"| --- | ---: | ---: |"
$files | Group-Object Extension | Sort-Object { ($_.Group | Measure-Object Length -Sum).Sum } -Descending | ForEach-Object {
    "| ``$($_.Name)`` | $($_.Count) | $(Format-MB ($_.Group | Measure-Object Length -Sum).Sum) |"
}
""
"## Executables"
""
Get-ChildItem $root -Filter *.exe -File | ForEach-Object {
    "- ``$($_.Name)``: file version $($_.VersionInfo.FileVersion), product version $($_.VersionInfo.ProductVersion)"
}
