@echo off
rem ===========================================================================
rem  INSTALL.bat  --  installs the chams fork as BlueStacks' opengl32.dll
rem
rem  Before running:
rem     put ReShade64.dll (from the GitHub Actions artifact) in THIS folder,
rem     or rename it to opengl32.dll here.
rem
rem  Usage:
rem     INSTALL.bat
rem     INSTALL.bat "C:\Program Files\BlueStacks_nxt"
rem ===========================================================================
setlocal enabledelayedexpansion
set "HERE=%~dp0"
set "DST=%~1"
set "SRC="

if exist "%HERE%opengl32.dll"      set "SRC=%HERE%opengl32.dll"
if not defined SRC if exist "%HERE%ReShade64.dll" set "SRC=%HERE%ReShade64.dll"

if not defined SRC (
    echo.
    echo   No DLL found in this folder.
    echo   Download the artifact "ReShade64-chams" from the Actions tab,
    echo   unzip it here ^(ReShade64.dll^) and run this again.
    echo.
    exit /b 1
)
echo DLL to install : %SRC%

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
    ) do (
        if exist "%%~D\HD-Player.exe" set "DST=%%~D"
    )
)
if "%DST%"=="" (
    echo BlueStacks folder not found. Run:  INSTALL.bat "C:\Program Files\BlueStacks_nxt"
    exit /b 1
)
echo BlueStacks folder : %DST%
echo.

rem keep whatever is there now, so uninstalling restores it
if exist "%DST%\opengl32.dll" (
    if exist "%DST%\opengl32_before_chams.dll" del /f /q "%DST%\opengl32_before_chams.dll"
    move /y "%DST%\opengl32.dll" "%DST%\opengl32_before_chams.dll" >nul
    echo [backup] old opengl32.dll -^> opengl32_before_chams.dll
)

copy /y "%SRC%" "%DST%\opengl32.dll" >nul || (
    echo ERROR: copy failed. Close BlueStacks / run as administrator.
    exit /b 1
)
echo [install] opengl32.dll  ^(chams fork^)
echo.
echo   BlueStacks - Settings - Graphics - Graphics engine = OPENGL  ^(mandatory^)
echo   Start it, press INSERT  =  the chams menu.
echo   Settings live in ReShade.ini next to HD-Player.exe, section [CHAMS].
echo   Log:      ReShade.log next to HD-Player.exe.
echo   Undo:     UNINSTALL.bat
