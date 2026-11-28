@echo off
setlocal
rem MK9 Access: MK9 by Lauta itself when this folder has none yet, then the settings the mod needs
rem (MK9Access\setup.ps1), the game files (install.ps1), then the game, with its saves in the game folder.
if exist "%~dp0mk9.exe" goto have_port

echo MK9 Access: getting MK9 by Lauta.
call :check_powershell
rem 1. a port the player downloaded already: unzipped into its own folder here, or its zip here or in Downloads
call :find_local
if exist "%~dp0mk9.exe" goto have_port
rem 2. the latest from GitHub (MK9Access\mk9update.ps1 says each step here)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0MK9Access\mk9update.ps1" -GameDir "%~dp0."
if exist "%~dp0mk9.exe" goto have_port
rem 3. the same download with the curl and tar that come with Windows, for when PowerShell cannot download
call :curl_port
if exist "%~dp0mk9.exe" goto have_port

echo.
echo MK9 by Lauta could not be downloaded. Check the internet connection and start MK9 Access launch.cmd again.
echo Or download it yourself from github.com/machinebreak/mk9-lauta/releases: the file that ends in windows-x64.zip.
echo Leave the zip in your Downloads folder or put it in this folder, and start MK9 Access launch.cmd again: it unpacks it by itself.
echo If you unzip it yourself, mk9.exe has to end up in this same folder, next to MK9 Access launch.cmd.
echo Press any key to close this window.
pause >nul
exit /b 1

:have_port
powershell -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File "%~dp0MK9Access\setup.ps1" -GameDir "%~dp0."
rem the game files from the player's .iso the first time (MK9Access\install.ps1 asks here in the console: the port's
rem own installer window cannot be used with Tab). 0 = files there, 3 = it started the game, 2 = quit.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0MK9Access\install.ps1" -GameDir "%~dp0."
if errorlevel 2 exit /b 0
rem the controller keeps working when the game window is not in front (the mod sets it for the copies the game starts)
set SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS=1
start "" "%~dp0mk9.exe" --mk9_plugins=true --user_data_root="%~dp0saves"
exit /b 0

:check_powershell
rem Smart App Control, AppLocker or a security program can put PowerShell in Constrained Language mode, and the
rem mod's scripts then stop without a word. Say so instead.
set "MK9A_LM="
for /f "delims=" %%M in ('powershell -NoProfile -Command "$ExecutionContext.SessionState.LanguageMode" 2^>nul') do set "MK9A_LM=%%M"
if /i "%MK9A_LM%"=="FullLanguage" exit /b 0
if not defined MK9A_LM (echo Note: PowerShell could not be started on this computer. MK9 Access needs it for its setup and updates. & exit /b 0)
echo Note: Windows limits PowerShell on this computer (language mode %MK9A_LM%). MK9 Access needs PowerShell for its setup and updates.
echo This usually comes from Smart App Control (Windows Security, App and browser control) or a security program.
exit /b 0

:find_local
rem Windows' Extract All puts the port into a folder of the zip's name (mk9-lauta-v1.4.4-windows-x64).
for /d %%D in ("%~dp0mk9-lauta*") do if exist "%%~fD\mk9.exe" if not exist "%~dp0mk9.exe" call :move_up "%%~fD"
if exist "%~dp0mk9.exe" exit /b 0
set "MK9A_ZIP="
for %%Z in ("%USERPROFILE%\Downloads\mk9-lauta-*windows-x64*.zip" "%~dp0mk9-lauta-*windows-x64*.zip") do set "MK9A_ZIP=%%~fZ"
if not defined MK9A_ZIP exit /b 0
for %%Z in ("%MK9A_ZIP%") do echo Found the MK9 by Lauta zip %%~nxZ. Unpacking it here.
"%SystemRoot%\System32\tar.exe" -xf "%MK9A_ZIP%" -C "%~dp0."
if exist "%~dp0mk9.exe" (echo MK9 by Lauta is installed.) else (echo That zip could not be unpacked. Trying the download instead.)
exit /b 0

:move_up
echo Found MK9 by Lauta in the folder %~nx1. Moving it next to MK9 Access launch.cmd.
robocopy "%~1" "%~dp0." /E /MOVE /NFL /NDL /NJH /NJS /NP >nul
if exist "%~dp0mk9.exe" echo MK9 by Lauta is installed.
exit /b 0

:curl_port
if not exist "%SystemRoot%\System32\curl.exe" exit /b 0
echo Trying the download once more with the download tool that comes with Windows.
set "MK9A_TAG="
for /f "delims=" %%U in ('%SystemRoot%\System32\curl.exe -s -o NUL -w "%%{redirect_url}" https://github.com/machinebreak/mk9-lauta/releases/latest') do set "MK9A_TAG=%%U"
if not defined MK9A_TAG (echo GitHub could not be reached. & exit /b 0)
set "MK9A_TAG=%MK9A_TAG:*/tag/=%"
set "MK9A_VER=%MK9A_TAG:v=%"
set "MK9A_DL=%TEMP%\mk9-lauta-%MK9A_TAG%-windows-x64.zip"
echo Downloading MK9 by Lauta %MK9A_VER%, about 75 MB. This can take a few minutes.
"%SystemRoot%\System32\curl.exe" -s -S -L --fail -o "%MK9A_DL%" "https://github.com/machinebreak/mk9-lauta/releases/download/%MK9A_TAG%/mk9-lauta-%MK9A_TAG%-windows-x64.zip"
if errorlevel 1 (echo That download did not work either. & del "%MK9A_DL%" 2>nul & exit /b 0)
echo Download finished. Unpacking.
"%SystemRoot%\System32\tar.exe" -xf "%MK9A_DL%" -C "%~dp0."
del "%MK9A_DL%" 2>nul
if exist "%~dp0mk9.exe" echo MK9 by Lauta %MK9A_VER% is installed.
exit /b 0
