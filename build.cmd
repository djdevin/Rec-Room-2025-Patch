@echo off
rem Wrapper around build.ps1 so the build works from cmd.exe / Explorer.
rem Any arguments are forwarded, e.g.: build.cmd -Rebuild
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1" %*
exit /b %ERRORLEVEL%
