@echo off
setlocal EnableExtensions

set "APP_NAME=DisplaySwap"
set "INSTALL_DIR=%LocalAppData%\%APP_NAME%"
set "REG_KEY=HKCU\Software\Microsoft\Windows\CurrentVersion\Run"
set "TASK_NAME=DisplaySwapDaemon"

echo.
echo ==========================================
echo   %APP_NAME% User-Mode Uninstaller
echo ==========================================
echo.

echo [1/4] Stopping daemon processes if running...
taskkill /F /IM DisplaySwapDaemon.exe >nul 2>&1
taskkill /F /IM DisplaySwap.exe >nul 2>&1

echo [2/4] Removing auto-start registry entry and legacy tasks...
:: 레지스트리에 등록했던 자동 실행 키 제거
reg delete "%REG_KEY%" /v "%APP_NAME%Daemon" /f >nul 2>&1
:: 예전에 등록되었을 수 있는 작업 스케줄러 항목도 함께 정리
schtasks /Delete /TN "%TASK_NAME%" /F >nul 2>&1

echo [3/4] Removing installation directory...
if exist "%INSTALL_DIR%" (
    rmdir /S /Q "%INSTALL_DIR%"
)

echo [4/4] Removing installation path from user PATH...
for /f "tokens=2*" %%a in ('reg query "HKCU\Environment" /v Path 2^>nul') do set "CURRENT_PATH=%%b"
if defined CURRENT_PATH (
    powershell -NoProfile -ExecutionPolicy Bypass -Command "$path = [Environment]::GetEnvironmentVariable('Path', 'User'); $target = [Environment]::ExpandEnvironmentVariables('%INSTALL_DIR%'); if ($path -like '*;*') { $newPath = (($path -split ';') | Where-Object { $_.Trim() -ne $target.Trim() }) -join ';' } else { if ($path.Trim() -eq $target.Trim()) { $newPath = '' } else { $newPath = $path } }; [Environment]::SetEnvironmentVariable('Path', $newPath, 'User')"
)

echo.
echo ==========================================
echo   Uninstallation completed successfully!
echo ==========================================
echo.
pause
exit /b 0