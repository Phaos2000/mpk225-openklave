@echo off
setlocal
title MPK225 USB Inventory - Read Only
echo ============================================================
echo MPK225 USB Inventory - read-only, no firmware changes
echo ============================================================
echo.
echo This window stays open so you can see the result.
echo.
powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "%~dp0tools\mpk225_usb_inventory.ps1"
set "status=%ERRORLEVEL%"
echo.
if not "%status%"=="0" (
  echo Probe returned status %status%. Read the message above.
) else (
  echo USB inventory completed.
)
echo.
echo Press any key to close this window.
pause >nul
exit /b %status%
