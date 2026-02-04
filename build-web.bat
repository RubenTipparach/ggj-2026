@echo off
echo Building web distribution...

setlocal
set PROJECT_ROOT=%~dp0
set ISO_WORK=%PROJECT_ROOT%build\iso_work

REM Create iso_work directory if needed
if not exist "%ISO_WORK%" mkdir "%ISO_WORK%"

REM Create proper iso.xml with non-timestamped names
(
echo ^<?xml version="1.0" encoding="UTF-8"?^>
echo.
echo ^<iso_project image_name="revenants_of_elmoria.bin" cue_sheet="revenants_of_elmoria.cue"^>
echo 	^<track type="data"^>
echo 		^<directory_tree^>
echo 			^<file name="SYSTEM.CNF" source="system.cnf"/^>
echo 			^<file name="LANDER.EXE" source="lander.psexe"/^>
echo 		^</directory_tree^>
echo 	^</track^>
echo 	^<track type="audio" source="track02_intro.wav"/^>
echo 	^<track type="audio" source="track03_gameplay.wav"/^>
echo ^</iso_project^>
) > "%ISO_WORK%\iso.xml"

REM Create system.cnf if it doesn't exist
if not exist "%ISO_WORK%\system.cnf" (
    echo BOOT=cdrom:\LANDER.EXE;1> "%ISO_WORK%\system.cnf"
    echo TCB=4>> "%ISO_WORK%\system.cnf"
    echo EVENT=10>> "%ISO_WORK%\system.cnf"
    echo STACK=801FFFF0>> "%ISO_WORK%\system.cnf"
)

REM Clean up old timestamped files and old lander files
del /Q "%ISO_WORK%\lander_*.bin" 2>nul
del /Q "%ISO_WORK%\lander_*.cue" 2>nul
del /Q "%ISO_WORK%\lander.bin" 2>nul
del /Q "%ISO_WORK%\lander.cue" 2>nul
del /Q "%ISO_WORK%\revenants_of_elmoria_*.bin" 2>nul
del /Q "%ISO_WORK%\revenants_of_elmoria_*.cue" 2>nul

REM Run CMake build-web target
cmake --build build --target build-web
if %ERRORLEVEL% EQU 0 (
    echo.
    echo Build successful!
    echo   ROM: web\rom\revenants_of_elmoria.rom
    echo   Game zip: build\revenants_of_elmoria.zip ^(bin+cue for emulator distribution^)
    echo   Web package: build\web_build.zip ^(for itch.io web upload^)
) else (
    echo.
    echo Build failed!
    exit /b 1
)

endlocal
