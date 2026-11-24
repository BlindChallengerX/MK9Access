@echo off
rem MK9 Access: MK9 by Lauta itself when this folder has none yet (MK9Access\mk9update.ps1 downloads the latest),
rem then the settings the mod needs (MK9Access\setup.ps1), the game files (install.ps1), then the game, with its saves in the game folder.
if not exist "%~dp0mk9.exe" (
  echo MK9 Access: getting MK9 by Lauta.
  powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0MK9Access\mk9update.ps1" -GameDir "%~dp0."
  if not exist "%~dp0mk9.exe" (
    echo MK9 by Lauta could not be downloaded. Check the internet connection and start MK9 Access launch.cmd again.
    echo You can also download it yourself from github.com/machinebreak/mk9-lauta/releases and unzip it into this folder.
    echo Press any key to close this window.
    pause >nul
    exit /b 1
  )
)
powershell -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File "%~dp0MK9Access\setup.ps1" -GameDir "%~dp0."
rem the game files from the player's .iso the first time (MK9Access\install.ps1 asks here in the console: the port's
rem own installer window cannot be used with Tab). 0 = files there, 3 = it started the game, 2 = quit.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0MK9Access\install.ps1" -GameDir "%~dp0."
if errorlevel 2 exit /b 0
start "" "%~dp0mk9.exe" --mk9_plugins=true --user_data_root="%~dp0saves"
