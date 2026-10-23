@echo off
setlocal
cd /d "%~dp0"

where cl.exe >nul 2>nul
if errorlevel 1 (
  echo cl.exe not found. Open "x64 Native Tools Command Prompt for VS" first.
  exit /b 1
)

cl.exe /nologo /std:c++17 /O2 /EHsc /W4 /WX /DWINVER=0x0A00 ^
  /D_WIN32_WINNT=0x0A00 /DUNICODE /D_UNICODE ^
  CloudLatencyFPS.cpp /link /SUBSYSTEM:WINDOWS,6.1 /STACK:4194304 /OPT:REF /OPT:ICF ^
  opengl32.lib dwmapi.lib comctl32.lib shell32.lib user32.lib gdi32.lib kernel32.lib ^
  /OUT:CloudLatencyFPS.exe

if errorlevel 1 exit /b 1
echo Built: %CD%\CloudLatencyFPS.exe
