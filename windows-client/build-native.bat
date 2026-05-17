@echo off
REM Native Windows build script using MSVC (cl.exe) or MinGW (g++)
REM Run from a Visual Studio Developer Command Prompt, or MinGW shell

setlocal

SET OUT_DIR=build-win
if not exist %OUT_DIR% mkdir %OUT_DIR%

REM ── Try MSVC (cl.exe) ────────────────────────────────────────────────────
where cl >nul 2>&1
if %errorlevel% == 0 (
    echo Using MSVC compiler...
    cl.exe /std:c++17 /O2 /W3 /EHsc ^
        /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0601 ^
        /Fe:%OUT_DIR%\cafe-client.exe ^
        main.cpp WsClient.cpp ^
        /link ws2_32.lib comctl32.lib /SUBSYSTEM:WINDOWS
    goto :done
)

REM ── Try MinGW (g++) ──────────────────────────────────────────────────────
where g++ >nul 2>&1
if %errorlevel% == 0 (
    echo Using MinGW g++ compiler...
    g++ -std=c++17 -O2 -mwindows ^
        -DWIN32_LEAN_AND_MEAN -D_WIN32_WINNT=0x0601 ^
        main.cpp WsClient.cpp ^
        -lws2_32 -lcomctl32 ^
        -static-libgcc -static-libstdc++ ^
        -o %OUT_DIR%\cafe-client.exe
    goto :done
)

echo ERROR: No C++ compiler found.
echo Install Visual Studio Build Tools or MinGW-w64.
exit /b 1

:done
echo.
echo Build complete! Output: %OUT_DIR%\cafe-client.exe
echo Run cafe-client.exe on each workstation, enter server IP and port.
endlocal
