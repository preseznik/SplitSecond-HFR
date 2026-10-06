<#
  Deploy / remove the mod in the game folder, and back up saves.

  deploy.ps1 -Dll <path> -GameDir <dir> -Ini <path>   copy DLL; copy INI only if the game folder has none
  deploy.ps1 -GameDir <dir> -ForceIni -Ini <path>    overwrite the INI in the game folder
  deploy.ps1 -GameDir <dir> -Remove                  delete the mod files (logs are kept)
  deploy.ps1 -BackupSaves                            zip Documents\Disney Interactive Studios\Split Second
#>
param(
    [string]$Dll,
    [string]$GameDir = "C:\Program Files (x86)\Steam\steamapps\common\SplitSecond",
    [string]$Ini,
    [switch]$ForceIni,
    [switch]$Remove,
    [switch]$BackupSaves
)
$ErrorActionPreference = 'Stop'

if ($BackupSaves) {
    $saves = Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'Disney Interactive Studios\Split Second'
    if (Test-Path $saves) {
        $dest = Join-Path $PSScriptRoot "..\backups"
        New-Item -ItemType Directory -Force $dest | Out-Null
        $zip = Join-Path $dest ("saves-{0:yyyyMMdd-HHmmss}.zip" -f (Get-Date))
        Compress-Archive -Path "$saves\*" -DestinationPath $zip
        Write-Host "Saves backed up to $zip"
    } else {
        Write-Host "No save folder yet ($saves)"
    }
}

if ($Remove) {
    foreach ($f in 'DXFreezerServer.dll', 'DXFreezerServer.pdb', 'SplitSecondHFR.ini') {
        $p = Join-Path $GameDir $f
        if (Test-Path $p) { Remove-Item $p; Write-Host "Removed $p" }
    }
    return
}

if ($Dll) {
    if (Get-Process SplitSecond -ErrorAction SilentlyContinue) { throw "Split/Second is running - close it before deploying." }
    Copy-Item $Dll (Join-Path $GameDir 'DXFreezerServer.dll') -Force
    $pdb = [IO.Path]::ChangeExtension($Dll, '.pdb')
    if (Test-Path $pdb) { Copy-Item $pdb (Join-Path $GameDir 'DXFreezerServer.pdb') -Force }
    Write-Host "Deployed DLL to $GameDir"
}
if ($Ini) {
    $target = Join-Path $GameDir 'SplitSecondHFR.ini'
    if ($ForceIni -or -not (Test-Path $target)) {
        Copy-Item $Ini $target -Force
        Write-Host "Deployed INI to $target"
    } else {
        Write-Host "Kept existing INI ($target); use -ForceIni to overwrite"
    }
}
