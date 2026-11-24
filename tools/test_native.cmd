@echo off
setlocal
cd /d "%~dp0.."
if not exist ".build\native" mkdir ".build\native"
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /I tests\native tests\native\test_bridge.cpp /Fe:.build\native\test_bridge.exe /Fo:.build\native\test_bridge.obj
if errorlevel 1 exit /b 1
.build\native\test_bridge.exe
exit /b %errorlevel%
