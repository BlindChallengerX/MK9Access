@echo off
rem MK9 Access: the settings the mod needs (MK9Access\setup.ps1), then the game, with its saves in the game folder.
powershell -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File "%~dp0MK9Access\setup.ps1" -GameDir "%~dp0."
start "" "%~dp0mk9.exe" --mk9_plugins=true --user_data_root="%~dp0saves"
