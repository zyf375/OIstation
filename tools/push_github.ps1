# OIstation → GitHub 一键推送
# 用法：右键「使用 PowerShell 运行」，或
#   powershell -ExecutionPolicy Bypass -File tools\push_github.ps1 -RepoUrl https://github.com/<你的用户名>/<仓库名>.git
param(
    [Parameter(Mandatory = $true)][string]$RepoUrl
)

$ErrorActionPreference = 'Stop'
$git = 'D:\dev\git\Git\cmd\git.exe'
$proj = 'E:\LL\OIstation'

if (-not (Test-Path $git)) { $git = 'git' }
Set-Location $proj

# 本机 schannel 会被中途断开，改用 OpenSSL 后端 + HTTP/1.1
& $git config http.sslBackend openssl
& $git config http.version HTTP/1.1
& $git config user.name '934818913'
& $git config user.email '934818913@qq.com'

try { & $git remote remove origin } catch { }
& $git remote add origin $RepoUrl

Write-Host "推送 $proj -> $RepoUrl ..." -ForegroundColor Cyan
Write-Host "首次会弹出 GitHub 登录窗口（浏览器授权即可，不用输密码）" -ForegroundColor Yellow
& $git push -u origin main

if ($LASTEXITCODE -eq 0) {
    Write-Host "`n✅ 推送成功" -ForegroundColor Green
    & $git log --oneline -1
} else {
    Write-Host "`n❌ 推送失败：若提示 Repository not found，请先在 GitHub 上新建空仓库（不要勾选 README）" -ForegroundColor Red
}
