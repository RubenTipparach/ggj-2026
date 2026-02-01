@echo off
REM Generate world data from neighborhood.png
REM This regenerates src/game/world_data.h and src/game/world_data.c

echo Generating map data from assets/neighborhood.png...
python tools/parseNeighborhood.py assets/neighborhood.png src/game/world_data.h src/game/world_data.c

if %ERRORLEVEL% EQU 0 (
    echo.
    echo Map generation complete!
    echo Output files:
    echo   - src/game/world_data.h
    echo   - src/game/world_data.c
    echo.
    echo Run 'cmake --build build' to rebuild the game with the new map data.
) else (
    echo.
    echo ERROR: Map generation failed!
    exit /b 1
)
