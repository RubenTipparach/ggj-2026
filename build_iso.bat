@echo off
REM Build PSX disc image (.bin/.cue) from compiled executable
REM Requires mkpsxiso - download from https://github.com/Lameguy64/mkpsxiso/releases

setlocal

set PROJECT_ROOT=%~dp0
set BUILD_DIR=%PROJECT_ROOT%build
set OUTPUT_DIR=%PROJECT_ROOT%web\rom
set WORK_DIR=%BUILD_DIR%\iso_work

REM Check if executable exists
if not exist "%BUILD_DIR%\lander.psexe" (
    echo ERROR: lander.psexe not found!
    echo Run 'cmake --build build' first
    exit /b 1
)

REM Look for mkpsxiso
set MKPSXISO=
if exist "%PROJECT_ROOT%tools\mkpsxiso\mkpsxiso.exe" set MKPSXISO=%PROJECT_ROOT%tools\mkpsxiso\mkpsxiso.exe
if exist "%PROJECT_ROOT%tools\mkpsxiso.exe" set MKPSXISO=%PROJECT_ROOT%tools\mkpsxiso.exe
where mkpsxiso >nul 2>&1 && set MKPSXISO=mkpsxiso

if "%MKPSXISO%"=="" (
    echo ERROR: mkpsxiso not found!
    echo.
    echo Run install.bat first to download mkpsxiso
    echo Or download manually from: https://github.com/Lameguy64/mkpsxiso/releases
    exit /b 1
)

echo Using mkpsxiso: %MKPSXISO%

REM Create working directory
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
if not exist "%WORK_DIR%" mkdir "%WORK_DIR%"
echo Working directory: %WORK_DIR%

REM Copy executable
copy /Y "%BUILD_DIR%\lander.psexe" "%WORK_DIR%\lander.psexe" >nul

REM Copy audio tracks from assets/Music
set MUSIC_DIR=%PROJECT_ROOT%assets\Music
if exist "%MUSIC_DIR%\Intro_V3.wav" copy /Y "%MUSIC_DIR%\Intro_V3.wav" "%WORK_DIR%\track02_intro.wav" >nul
if exist "%MUSIC_DIR%\Gameplay_Loop.wav" copy /Y "%MUSIC_DIR%\Gameplay_Loop.wav" "%WORK_DIR%\track03_gameplay.wav" >nul

REM Create SYSTEM.CNF
echo BOOT=cdrom:\LANDER.EXE;1> "%WORK_DIR%\system.cnf"
echo TCB=4>> "%WORK_DIR%\system.cnf"
echo EVENT=10>> "%WORK_DIR%\system.cnf"
echo STACK=801FFFF0>> "%WORK_DIR%\system.cnf"

REM Create ISO XML configuration with audio tracks
(
echo ^<?xml version="1.0" encoding="UTF-8"?^>
echo ^<iso_project image_name="lander.bin" cue_sheet="lander.cue"^>
echo     ^<track type="data"^>
echo         ^<directory_tree^>
echo             ^<file name="SYSTEM.CNF" source="system.cnf"/^>
echo             ^<file name="LANDER.EXE" source="lander.psexe"/^>
echo         ^</directory_tree^>
echo     ^</track^>
echo     ^<track type="audio" source="track02_intro.wav"/^>
echo     ^<track type="audio" source="track03_gameplay.wav"/^>
echo ^</iso_project^>
) > "%WORK_DIR%\iso.xml"

REM Delete old files to avoid lock issues
if exist "%WORK_DIR%\lander.bin" del /F "%WORK_DIR%\lander.bin"
if exist "%WORK_DIR%\lander.cue" del /F "%WORK_DIR%\lander.cue"

REM Build disc image (must run from work dir for relative paths in XML)
echo Building disc image...
pushd "%WORK_DIR%"
"%MKPSXISO%" iso.xml -y
if errorlevel 1 (
    echo ERROR: mkpsxiso failed!
    popd
    exit /b 1
)
popd

REM Copy output to web/rom
if not exist "%OUTPUT_DIR%" mkdir "%OUTPUT_DIR%"
if exist "%OUTPUT_DIR%\lander.bin" del /F "%OUTPUT_DIR%\lander.bin"
if exist "%OUTPUT_DIR%\lander.cue" del /F "%OUTPUT_DIR%\lander.cue"
copy /Y "%WORK_DIR%\lander.bin" "%OUTPUT_DIR%\lander.bin" >nul
copy /Y "%WORK_DIR%\lander.cue" "%OUTPUT_DIR%\lander.cue" >nul

REM Create lander.rom (renamed .zip) containing both .bin and .cue for EmulatorJS
REM itch.io blocks fetching .zip files with 403, but .rom extension works
if exist "%OUTPUT_DIR%\lander.rom" del /F "%OUTPUT_DIR%\lander.rom"
if exist "%OUTPUT_DIR%\lander.zip" del /F "%OUTPUT_DIR%\lander.zip"
pushd "%OUTPUT_DIR%"
powershell -NoProfile -ExecutionPolicy Bypass -Command "Compress-Archive -Path 'lander.bin','lander.cue' -DestinationPath 'lander.zip' -Force"
ren lander.zip lander.rom
popd

echo.
echo SUCCESS! Created:
echo   %OUTPUT_DIR%\lander.bin
echo   %OUTPUT_DIR%\lander.cue
echo   %OUTPUT_DIR%\lander.rom ^(zip with .bin+.cue for EmulatorJS^)

REM Create web_build.zip with all files needed for itch.io deployment
set WEB_DIR=%PROJECT_ROOT%web
set WEB_BUILD_ZIP=%BUILD_DIR%\web_build.zip

echo.
echo Creating web deployment package for itch.io...

REM Delete old zip if exists
if exist "%WEB_BUILD_ZIP%" del /F "%WEB_BUILD_ZIP%"

REM Create the zip with all web files using PowerShell
REM Need to include: index.html, bios/, data/, emulatorjs/, rom/
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
    "$webDir = '%WEB_DIR%'; " ^
    "$zipPath = '%WEB_BUILD_ZIP%'; " ^
    "$items = @( " ^
    "    (Join-Path $webDir 'index.html'), " ^
    "    (Join-Path $webDir 'bios'), " ^
    "    (Join-Path $webDir 'data'), " ^
    "    (Join-Path $webDir 'emulatorjs'), " ^
    "    (Join-Path $webDir 'rom') " ^
    "); " ^
    "Compress-Archive -Path $items -DestinationPath $zipPath -Force; " ^
    "$size = (Get-Item $zipPath).Length / 1MB; " ^
    "Write-Host ('Created: ' + $zipPath + ' (' + [math]::Round($size, 2) + ' MB)')"

if errorlevel 1 (
    echo WARNING: Failed to create web_build.zip
    echo Trying alternative method...
    REM Fallback: use tar if available
    pushd "%WEB_DIR%"
    tar -acf "%WEB_BUILD_ZIP%" index.html bios data emulatorjs rom 2>nul
    popd
    if exist "%WEB_BUILD_ZIP%" (
        echo Created web_build.zip using tar
    ) else (
        echo FAILED to create web_build.zip
    )
) else (
    echo.
    echo Web package includes:
    echo   - index.html
    echo   - bios/ ^(PS1 BIOS files^)
    echo   - data/ ^(EmulatorJS data^)
    echo   - emulatorjs/ ^(EmulatorJS core~3MB^)
    echo   - rom/lander.rom ^(zip with bin+cue for EmulatorJS^)
)

echo.
echo To test locally, refresh the web page.
echo To deploy on itch.io:
echo   1. Create a new HTML5 project on itch.io
echo   2. Upload web_build.zip
echo   3. Set index.html as the launch file
echo   4. Enable SharedArrayBuffer in embed options

endlocal
