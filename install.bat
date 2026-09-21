@echo off
setlocal EnableExtensions

set "APP_NAME=DisplaySwap"
set "INSTALL_DIR=%LocalAppData%\%APP_NAME%"
set "REG_KEY=HKCU\Software\Microsoft\Windows\CurrentVersion\Run"

echo.
echo ==========================================
echo   %APP_NAME% User-Mode Installer
echo ==========================================
echo.

echo [1/4] Stopping existing daemon if running...
taskkill /F /IM DisplaySwapDaemon.exe >nul 2>&1

echo [2/4] Creating installation directory...
if not exist "%INSTALL_DIR%" mkdir "%INSTALL_DIR%"

echo [3/4] Copying binaries...
if exist "build\Release\DisplaySwapDaemon.exe" (
    copy /Y "build\Release\DisplaySwapDaemon.exe" "%INSTALL_DIR%\" >nul
) else (
    echo [Error] DisplaySwapDaemon.exe not found. Please run build.bat first.
    pause
    exit /b 1
)

if exist "build\Release\DisplaySwap.exe" (
    copy /Y "build\Release\DisplaySwap.exe" "%INSTALL_DIR%\" >nul
) else (
    echo [Error] DisplaySwap.exe not found. Please run build.bat first.
    pause
    exit /b 1
)

echo [4/4] Registering auto-start and updating PATH...
:: 레지스트리 Run 키에 등록하여 로그인 시 백그라운드 자동 실행 설정 (관리자 권한 불필요)
reg add "%REG_KEY%" /v "%APP_NAME%Daemon" /t REG_SZ /d "\"%INSTALL_DIR%\DisplaySwapDaemon.exe\"" /f >nul

:: 사용자 환경 변수(PATH)에 설치 경로 추가
for /f "tokens=2*" %%a in ('reg query "HKCU\Environment" /v Path 2^>nul') do set "CURRENT_PATH=%%b"
if not defined CURRENT_PATH set "CURRENT_PATH="

echo %CURRENT_PATH% | find /i "%INSTALL_DIR%" >nul
if errorlevel 1 (
    if defined CURRENT_PATH (
        setx PATH "%CURRENT_PATH%;%INSTALL_DIR%" >nul
    ) else (
        setx PATH "%INSTALL_DIR%" >nul
    )
)

echo.
echo ==========================================
echo   Installation completed successfully!
echo ==========================================
echo Note: Please restart your terminal to use 'DisplaySwap' globally.
echo.
pause
exit /b 0