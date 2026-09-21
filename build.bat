@echo off
setlocal EnableExtensions

set "SCRIPT_DIR=%~dp0"
cd /d "%SCRIPT_DIR%"

echo.
echo ==========================================
echo   DisplaySwap Build Script
echo ==========================================
echo.

:: 1. CMake 빌드 디렉터리 구성 (이미 있어도 안전하게 갱신)
echo [1/3] Configuring CMake project...
cmake -B build -S .
if errorlevel 1 (
    echo ERROR: CMake configuration failed.
    pause
    exit /b 1
)

:: 2. Release 모드로 빌드 실행
echo [2/3] Building Release configuration...
cmake --build build --config Release
if errorlevel 1 (
    echo ERROR: Build failed.
    pause
    exit /b 1
)

:: 3. 빌드된 실행 파일을 루트 디렉토리로 복사 (Git 추적용)
echo [3/3] Copying executables to root directory...
if exist "build\Release\DisplaySwapDaemon.exe" (
    copy /Y "build\Release\DisplaySwapDaemon.exe" . >nul
) else if exist "build\Release\DisplaySwapDeamon.exe" (
    copy /Y "build\Release\DisplaySwapDeamon.exe" . >nul
)

if exist "build\Release\DisplaySwap.exe" (
    copy /Y "build\Release\DisplaySwap.exe" . >nul
)

echo.
echo ==========================================
echo   Build and copy completed successfully!
echo ==========================================
echo.
pause
exit /b 0