@echo off
setlocal
title MPK225 USB Inventory - Read Only
cd /d "%~dp0"
echo ============================================================
echo MPK225 USB Inventory - read-only, no firmware changes
echo ============================================================
echo.
echo Scanning Windows USB / Plug and Play devices...
echo.
powershell.exe -NoProfile -NonInteractive -Command "$ErrorActionPreference='Stop'; try { $all=@(Get-CimInstance -ClassName Win32_PnPEntity); $found=@($all | Where-Object { ([string]$_.Name -match 'MPK.?225') -or ([string]$_.Description -match 'MPK.?225') }); $records=@(foreach($d in $found){ $id=[string]$d.PNPDeviceID; $m=[regex]::Match($id,'VID_([0-9A-Fa-f]{4})&PID_([0-9A-Fa-f]{4})'); $pair='unknown'; if($m.Success){ $pair=$m.Groups[1].Value.ToUpper()+':'+$m.Groups[2].Value.ToUpper() }; [pscustomobject]@{ Name=[string]$d.Name; VID_PID=$pair; Status=[string]$d.Status; Driver=[string]$d.Service } }); if($records.Count -eq 0){ Write-Host 'No MPK225-named device was found. Check USB connection and Device Manager.' -ForegroundColor Yellow; exit 1 }; Write-Host ('Detected {0} MPK225 device record(s):' -f $records.Count) -ForegroundColor Green; $records | Format-Table -AutoSize | Out-Host; $records | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath 'mpk225-usb-report.json' -Encoding UTF8; Write-Host 'Saved: mpk225-usb-report.json'; exit 0 } catch { Write-Host ('USB inventory error: '+$_.Exception.Message) -ForegroundColor Red; exit 2 }"
set "status=%ERRORLEVEL%"
echo.
if "%status%"=="0" (
  echo USB inventory completed successfully.
) else (
  echo Probe returned status %status%.
)
echo.
echo Press any key to close this window.
pause >nul
exit /b %status%
