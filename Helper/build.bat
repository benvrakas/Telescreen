@echo off
rem Builds Helper\out\TelescreenHost.exe with MSVC against the pinned FFmpeg LGPL build and CEF (libcef.dll is
rem delay-loaded from the browser runtime component; see Docs/HostProtocol.md).
setlocal
set FFMPEG=D:\SFMod\Tools\ffmpeg\ffmpeg-n8.1.3-win64-lgpl-shared-8.1
set CEF=D:\SFMod\Tools\cef\cef-152.0.15
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
cd /d "%~dp0"
if not exist out mkdir out
if not exist "%CEF%\build\libcef_dll_wrapper\Release\libcef_dll_wrapper.lib" call D:\SFMod\Tools\cef\build_wrapper.bat || exit /b 1
cl /nologo /std:c++20 /EHsc /O2 /W3 /MD /DUNICODE /D_UNICODE /DWIN32 /D_WINDOWS /DNOMINMAX /DWIN32_LEAN_AND_MEAN ^
  /D_WIN32_WINNT=0x0A00 /DWINVER=0x0A00 /Zi /Brepro ^
  /I "%FFMPEG%\include" /I "%CEF%" /I ..\Source\Telescreen\Private ^
  src\main.cpp src\Common.cpp src\Pipe.cpp src\SharedMemory.cpp src\YtDlp.cpp src\MediaSession.cpp src\Security.cpp src\BrowserSession.cpp src\StreamCodec.cpp src\EncodeBench.cpp src\LoopbackSession.cpp ^
  /Fo:out\ /Fd:out\ /Fe:out\TelescreenHost.exe ^
  /link /SUBSYSTEM:CONSOLE /DEBUG /OPT:REF /Brepro /LIBPATH:"%FFMPEG%\lib" ^
  avformat.lib avcodec.lib avutil.lib swscale.lib swresample.lib winmm.lib user32.lib advapi32.lib ^
  "%CEF%\Release\libcef.lib" "%CEF%\build\libcef_dll_wrapper\Release\libcef_dll_wrapper.lib" delayimp.lib /DELAYLOAD:libcef.dll
if errorlevel 1 exit /b 1
for %%D in (avformat-62 avcodec-62 avutil-60 swscale-9 swresample-6) do copy /y "%FFMPEG%\bin\%%D.dll" out\ >nul
echo BUILD OK
