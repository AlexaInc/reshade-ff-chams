@echo off
rem  Restores the DLL that was there before INSTALL.bat.
setlocal enabledelayedexpansion
set "DST=%~1"
if "%DST%"=="" (
    for %%D in (
        "%ProgramFiles%\BlueStacks_nxt"
        "%ProgramFiles%\BlueStacks"
        "%ProgramFiles%\BlueStacks_msi5"
        "%ProgramFiles%\BlueStacks_msi2"
        "%ProgramFiles(x86)%\BlueStacks_nxt"
        "%ProgramFiles(x86)%\BlueStacks"
        "%LOCALAPPDATA%\BlueStacks_nxt"
        "%LOCALAPPDATA%\Programs\BlueStacks_nxt"
    ) do ( if exist "%%~D\HD-Player.exe" set "DST=%%~D" )
)
if "%DST%"=="" ( echo BlueStacks not found. & exit /b 1 )
echo BlueStacks folder : %DST%

if exist "%DST%\opengl32.dll" del /f /q "%DST%\opengl32.dll"
if exist "%DST%\opengl32_before_chams.dll" (
    move /y "%DST%\opengl32_before_chams.dll" "%DST%\opengl32.dll" >nul
    echo [restore] opengl32_before_chams.dll -^> opengl32.dll
) else (
    echo [ok] nothing to restore - BlueStacks uses the system OpenGL driver again
)
if exist "%DST%\ReShade.log" del /f /q "%DST%\ReShade.log"
echo done.
