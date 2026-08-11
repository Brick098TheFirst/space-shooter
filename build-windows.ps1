$ErrorActionPreference = "Stop"

Write-Host "Publishing Space Unlimited: Recharged for Windows x64..." -ForegroundColor Cyan

dotnet publish ".\SpaceUnlimited.Windows\SpaceUnlimited.Windows.csproj" `
    --configuration Release `
    --runtime win-x64 `
    --self-contained true `
    -p:PublishSingleFile=true `
    -p:DebugType=None `
    -p:DebugSymbols=false `
    --output ".\release\SpaceUnlimited-win-x64"

Write-Host ""
Write-Host "Build complete: release\SpaceUnlimited-win-x64\SpaceUnlimited.exe" -ForegroundColor Green
Write-Host "Keep the Assets folder next to the executable." -ForegroundColor Yellow
