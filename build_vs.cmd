@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >/dev/null
if errorlevel 1 exit /b 1
call "%~dp0build.bat" %*
