<#
 Publica VideoLab en GitHub (requiere Git y GitHub CLI: https://cli.github.com).
 Uso:  .\scripts\publish-github.ps1 -Name VideoLab -Visibility public
#>
param(
    [string]$Name = "VideoLab",
    [ValidateSet("public", "private")][string]$Visibility = "public"
)
$ErrorActionPreference = "Stop"
Set-Location (Split-Path $PSScriptRoot -Parent)

if (-not (Get-Command git -ErrorAction SilentlyContinue)) { throw "Instala Git: https://git-scm.com" }
if (-not (Get-Command gh  -ErrorAction SilentlyContinue)) { throw "Instala GitHub CLI: https://cli.github.com" }
gh auth status 2>$null | Out-Null
if ($LASTEXITCODE -ne 0) { gh auth login }

if (-not (Test-Path .git)) { git init -b main }
git add -A
git commit -m "VideoLab: editor de video/audio (MFC + FFmpeg 9)" 2>$null
gh repo create $Name --$Visibility --source . --remote origin --push
Write-Host "Listo. Repositorio publicado." -ForegroundColor Green
