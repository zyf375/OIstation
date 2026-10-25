# OIstation -> GitHub interactive push (ASCII only)
$ErrorActionPreference = 'Continue'
Set-Location 'E:\LL\OIstation'
'START ' + (Get-Date -Format 'HH:mm:ss') | Out-File -Encoding utf8 'E:\LL\OIstation\push.log'

$git = 'D:\dev\git\Git\cmd\git.exe'
if (-not (Test-Path $git)) { $git = 'git' }

Write-Host '========================================='
Write-Host ' OIstation -> GitHub push'
Write-Host ' A GitHub login window will pop up.'
Write-Host ' Click Authorize in the browser.'
Write-Host '========================================='

& $git -c http.sslBackend=openssl -c http.version=HTTP/1.1 push -u origin main *>&1 |
    Tee-Object -Append -FilePath 'E:\LL\OIstation\push.log'

'EXITCODE=' + $LASTEXITCODE | Out-File -Append -Encoding utf8 'E:\LL\OIstation\push.log'
Write-Host ''
Write-Host 'Done. Window stays open.'
