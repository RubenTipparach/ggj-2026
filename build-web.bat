@echo off
echo Building web distribution...
cmake --build build --target build-web
if %ERRORLEVEL% EQU 0 (
    echo.
    echo Build successful!
    echo   ROM: web\rom\lander.zip
    echo   Full package: build\web_build.zip
) else (
    echo.
    echo Build failed!
    exit /b 1
)
