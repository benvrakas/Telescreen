@echo off
rem Builds Helper\out\TelescreenHost.exe with MSVC against the pinned FFmpeg LGPL build.
setlocal
set FFMPEG=D:\SFMod\Tools\ffmpeg\ffmpeg-n8.1.3-win64-lgpl-shared-8.1
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
cd /d "%~dp0"
if not exist out mkdir out
cl /nologo /std:c++20 /EHsc /O2 /W3 /MD /DUNICODE /D_UNICODE /Zi /Brepro ^
  /I "%FFMPEG%\include" /I ..\Source\Telescreen\Private ^
  src\main.cpp src\Common.cpp src\Pipe.cpp src\SharedMemory.cpp src\YtDlp.cpp src\MediaSession.cpp src\Security.cpp ^
  /Fo:out\ /Fd:out\ /Fe:out\TelescreenHost.exe ^
  /link /SUBSYSTEM:CONSOLE /DEBUG /OPT:REF /Brepro /LIBPATH:"%FFMPEG%\lib" ^
  avformat.lib avcodec.lib avutil.lib swscale.lib swresample.lib winmm.lib user32.lib advapi32.lib
if errorlevel 1 exit /b 1
for %%D in (avformat-62 avcodec-62 avutil-60 swscale-9 swresample-6) do copy /y "%FFMPEG%\bin\%%D.dll" out\ >nul
echo BUILD OK
