@echo off
rem ===========================================================================
rem  CLEAN-FIRST.bat -- removes EVERYTHING the earlier proxy builds (v3..v10)
rem  put on this machine. Run it once before INSTALL.bat.
rem
rem  Deletes in the BlueStacks folder:
rem     opengl32.dll, opengl32_orig.dll, orig\opengl32.dll, opengl32.ini,
rem     opengl32.log, opengl32_backup.dll.bak, opengl32_before_chams.dll
rem  and per user:
rem     %LOCALAPPDATA%\opengl32\          (our old log folder)
rem     %LOCALAPPDATA%\opengl32_layer\    (staged copy of the old original)
rem     %TEMP%\opengl32.log
rem
rem  It does NOT touch BlueStacks itself, your games, or any ReShade file that
rem  is not one of ours.
rem ===========================================================================
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

echo ==========================================================
echo   FULL CLEAN of the old proxy builds
echo ==========================================================
if "%DST%"=="" (
    echo [warn] BlueStacks folder not found - per-user folders are cleaned anyway
) else (
    echo BlueStacks folder : %DST%
    for %%F in (opengl32.dll opengl32_orig.dll opengl32.ini opengl32.log opengl32_backup.dll.bak opengl32_before_chams.dll ReShade.ini) do (
        if exist "%DST%\%%F" (
            findstr /i /c:"[CHAMS]" "%DST%\%%F" >nul 2>&1
            if errorlevel 1 (
                rem not written by us -> only remove the files we always own
                if /i not "%%F"=="ReShade.ini" ( del /f /q "%DST%\%%F" & echo [delete] %%F )
            ) else (
                echo [keep]   %%F - it is the new chams config, left in place
            )
        )
    )
    if exist "%DST%\orig\opengl32.dll" ( del /f /q "%DST%\orig\opengl32.dll" & echo [delete] orig\opengl32.dll )
    if exist "%DST%\orig" rmdir "%DST%\orig" 2>nul
)
if exist "%LOCALAPPDATA%\opengl32" (
    del /f /q "%LOCALAPPDATA%\opengl32\*.*" 2>nul
    rmdir /s /q "%LOCALAPPDATA%\opengl32" 2>nul
    echo [delete] %LOCALAPPDATA%\opengl32
)
if exist "%LOCALAPPDATA%\opengl32_layer" (
    del /f /q "%LOCALAPPDATA%\opengl32_layer\*.*" 2>nul
    rmdir /s /q "%LOCALAPPDATA%\opengl32_layer" 2>nul
    echo [delete] %LOCALAPPDATA%\opengl32_layer
)
if exist "%TEMP%\opengl32.log" ( del /f /q "%TEMP%\opengl32.log" & echo [delete] %%TEMP%%\opengl32.log )
echo.
echo clean done. BlueStacks now uses C:\Windows\System32\opengl32.dll.
echo next: INSTALL.bat
pause
