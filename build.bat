@echo off
rem ---------------------------------------------------------------------------
rem IMDiscord - MSVC build without any CRT.
rem
rem Everything is compiled with /Zl (no default library directives) and linked
rem with /NODEFAULTLIB, so the only runtime is customcrt + Win32. The stack is
rem fully committed because the __chkstk stub does not probe guard pages.
rem
rem   build.bat            release build
rem   build.bat debug      debug build (/Od, console subsystem for OutputDebug)
rem   build.bat clean      wipe obj/bin
rem ---------------------------------------------------------------------------
setlocal enabledelayedexpansion

set "ROOT=%~dp0"
set "SRC=%ROOT%src"
set "OBJ=%ROOT%obj"
set "BIN=%ROOT%bin"

if /i "%~1"=="clean" (
    if exist "%OBJ%" rmdir /s /q "%OBJ%"
    if exist "%BIN%" rmdir /s /q "%BIN%"
    echo [build] cleaned
    exit /b 0
)

set "CONFIG=release"
if /i "%~1"=="debug" set "CONFIG=debug"

rem ---- toolchain ------------------------------------------------------------
rem Kept out of an if-block on purpose: %ProgramFiles(x86)% contains a closing
rem parenthesis that would terminate the block early.
if defined VCINSTALLDIR goto :have_toolchain

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" set "VSWHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo [build] vswhere.exe not found - install Visual Studio 2019/2022 with the C++ toolset.
    exit /b 1
)

set "VSPATH="
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"
if not defined VSPATH (
    echo [build] no Visual Studio installation with the C++ toolset was found.
    exit /b 1
)

call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 (
    echo [build] vcvars64.bat failed.
    exit /b 1
)

:have_toolchain

if not exist "%OBJ%" mkdir "%OBJ%"
if not exist "%BIN%" mkdir "%BIN%"

rem ---- flags ----------------------------------------------------------------
set INCLUDES=/I"%SRC%" /I"%SRC%\system\pch" /I"%SRC%\system\alghoritms" /I"%SRC%\system\io" /I"%SRC%\system\manipulators" /I"%SRC%\libs" /I"%SRC%\libs\customcrt" /I"%SRC%\libs\imgui" /I"%SRC%\libs\stb" /I"%SRC%\libs\opus\include" /I"%SRC%\libs\libwebp" /I"%SRC%\libs\speexdsp" /I"%SRC%\libs\tlse" /I"%SRC%\bin"

set COMMON=/nologo /c /std:c++17 /W3 /Zl /GS- /GR- /Gy /Gw /Oi /fp:precise /Zc:threadSafeInit- /D_HAS_EXCEPTIONS=0 /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS /wd4005 /wd4996 /wd4244 /wd4267 /wd4018 /wd4838 /wd4141 /wd4200 /wd4245 /wd4163 /wd4164

rem Development hooks: sign in with IMD_TOKEN, join IMD_AUTOJOIN, start a stream
rem when IMD_AUTOSHARE is set. Off unless IMD_TEST_HOOKS is exported, so a
rem release build never carries them.
if defined IMD_TEST_HOOKS set "COMMON=%COMMON% /DIMD_VOICE_TEST"

if "%CONFIG%"=="debug" (
    set "CFLAGS=%COMMON% /Od /Zi /DIMD_DEBUG=1 /FS /Fd"%OBJ%\imdiscord.pdb""
    set "OPUSOPT=/Od /DIMD_DEBUG=1"
    set "LFLAGS=/DEBUG /OPT:NOREF"
) else (
    set "CFLAGS=%COMMON% /O2 /DNDEBUG"
    set "OPUSOPT=/O2 /DNDEBUG"
    set "LFLAGS=/OPT:REF /OPT:ICF /RELEASE"
)

rem ---- precompiled header ---------------------------------------------------
echo [build] pch.h
cl %CFLAGS% %INCLUDES% /Ycpch.h /FIpch.h /Fp"%OBJ%\imdiscord.pch" /Fo"%OBJ%\pch.obj" "%SRC%\system\pch\pch.cpp"
if errorlevel 1 goto :fail

rem ---- tls ------------------------------------------------------------------
rem tlse is C, not C++, and wants none of the precompiled header. The
rem amalgamation switch is what pulls libtomcrypt and x509 in as one unit, so
rem this single file is the whole of it.
echo [build] tlse.c
rem ARGTYPE=4 turns libtomcrypt's argument checks into CRYPT_INVALID_ARG
rem returns instead of the default crypt_argchk, which drags in fprintf/raise.
cl %COMMON% /TC /D_NO_CRT_STDIO_INLINE /wd4267 /wd4244 /wd4133 /wd4101 /wd4189 /wd4127 /wd4706 /wd4310 /wd4245 ^
   /DTLS_AMALGAMATION /DTLS_FORWARD_SECRECY /DARGTYPE=4 /D_CRT_SECURE_NO_WARNINGS ^
   /I"%SRC%\libs\tlse" /Fo"%OBJ%\tlse.obj" "%SRC%\libs\tlse\tlse.c"
if errorlevel 1 goto :fail

rem ---- no-intrinsics math aliases -------------------------------------------
rem customcrt_mathalias.cpp defines names (sqrtf, pow, ...) that /Oi keeps as
rem intrinsics; it has to be compiled with intrinsics off and without the PCH.
echo [build] customcrt_mathalias.cpp
cl %COMMON% %OPUSOPT% /Oi- /I"%SRC%\libs\customcrt" /Fo"%OBJ%\customcrt_mathalias.obj" "%SRC%\libs\customcrt\customcrt_mathalias.cpp"
if errorlevel 1 goto :fail

rem ---- translation units ----------------------------------------------------
rem The object list outgrew the command line once libwebp and speexdsp joined,
rem so it is handed to the linker through a response file instead.
set "RSP=%OBJ%\objects.rsp"
echo "%OBJ%\pch.obj"> "%RSP%"
echo "%OBJ%\tlse.obj">> "%RSP%"
echo "%OBJ%\customcrt_mathalias.obj">> "%RSP%"
set /a INDEX=0

for /r "%SRC%" %%F in (*.cpp) do (
    set "FILE=%%F"
    set "SKIP="
    if /i "%%~nxF"=="pch.cpp"             set "SKIP=1"
    if /i "%%~nxF"=="imgui_demo.cpp"      set "SKIP=1"
    rem Built on its own line with /Oi-; see the section above.
    if /i "%%~nxF"=="customcrt_mathalias.cpp" set "SKIP=1"
    rem Only the speexdsp preprocessor is used; the rest of the library would
    rem drag in an echo canceller, a resampler and a jitter buffer for nothing.
    if /i "%%~nxF"=="buffer.cpp"          set "SKIP=1"
    if /i "%%~nxF"=="jitter.cpp"          set "SKIP=1"
    if /i "%%~nxF"=="resample.cpp"        set "SKIP=1"
    if /i "%%~nxF"=="scal.cpp"            set "SKIP=1"
    if /i "%%~nxF"=="smallft.cpp"         set "SKIP=1"

    rem Only WebP decoding is needed; the encoder and the mux layer are for
    rem producing files, which this client never does. demux stays in: discord
    rem serves animated avatars and banners as animated WebP, and WebPAnimDecoder
    rem lives there.
    set "DIR=%%~pF"
    if not "!DIR:\libwebp\enc\=!"=="!DIR!"      set "SKIP=1"
    if not "!DIR:\libwebp\mux\=!"=="!DIR!"      set "SKIP=1"
    if not "!DIR:\libwebp\sharpyuv\=!"=="!DIR!" set "SKIP=1"
    rem speexdsp only reads its config.h when HAVE_CONFIG_H is set, and that
    rem define must not leak into the other libraries.
    set "DIRPART=%%~pF"
    set "EXTRA="
    if not "!DIRPART:speexdsp=!"=="!DIRPART!" set "EXTRA=/DHAVE_CONFIG_H"

    if not defined SKIP (
        set /a INDEX+=1
        set "O=%OBJ%\u!INDEX!_%%~nF.obj"
        echo [build] %%~nxF
        cl %CFLAGS% !EXTRA! %INCLUDES% /Yupch.h /FIpch.h /Fp"%OBJ%\imdiscord.pch" /Fo"!O!" "%%F"
        if errorlevel 1 goto :fail
        echo "!O!">> "%RSP%"
    )
)

rem ---- opus -----------------------------------------------------------------
rem Compiled from the bundled 1.4 sources (src/libs/opus/source) instead of
rem linking the prebuilt lib: that fixed-point build returned full-scale
rem garbage on the concealment and high-complexity paths, which is what the
rem voice spikes were. SIMD dirs, the float silk pipeline, demos and tests
rem are excluded; allocation is routed into customcrt by opus_build_config.h,
rem so no CRT comes in with it. No precompiled header - this is C, not C++.
set "OPUSSRC=%SRC%\libs\opus\source"
set "OPUSINC=/I"%OPUSSRC%" /I"%OPUSSRC%\include" /I"%OPUSSRC%\celt" /I"%OPUSSRC%\silk" /I"%OPUSSRC%\silk\fixed" /I"%SRC%\libs\opus""

for /r "%OPUSSRC%" %%F in (*.c) do (
    set "SKIP="
    set "DIR=%%~pF"
    if not "!DIR:\x86\=!"=="!DIR!"    set "SKIP=1"
    if not "!DIR:\arm\=!"=="!DIR!"    set "SKIP=1"
    if not "!DIR:\mips\=!"=="!DIR!"   set "SKIP=1"
    if not "!DIR:\tests\=!"=="!DIR!"  set "SKIP=1"
    if not "!DIR:\float\=!"=="!DIR!"  set "SKIP=1"
    if /i "%%~nxF"=="opus_demo.c"           set "SKIP=1"
    if /i "%%~nxF"=="opus_compare.c"        set "SKIP=1"
    if /i "%%~nxF"=="repacketizer_demo.c"   set "SKIP=1"
    if /i "%%~nxF"=="opus_custom_demo.c"    set "SKIP=1"
    if /i "%%~nxF"=="opus_custom.c"         set "SKIP=1"
    if /i "%%~nxF"=="debug.c"               set "SKIP=1"

    if not defined SKIP (
        set /a INDEX+=1
        set "O=%OBJ%\u!INDEX!_%%~nF.obj"
        echo [build] %%~nxF
        cl %COMMON% %OPUSOPT% /TC /wd4305 /wd4334 /wd4701 /wd4703 /FI"%SRC%\libs\opus\opus_build_config.h" %OPUSINC% /Fo"!O!" "%%F"
        if errorlevel 1 goto :fail
        echo "!O!">> "%RSP%"
    )
)

rem ---- link -----------------------------------------------------------------
echo [build] linking
rem mfplat/mfuuid are the Media Foundation imports the H.264 encoder needs. Like
rem winhttp and d3d11 they are plain system DLLs, so no CRT comes with them.
set LIBS=kernel32.lib user32.lib gdi32.lib shell32.lib ole32.lib oleaut32.lib advapi32.lib comdlg32.lib ws2_32.lib dwmapi.lib imm32.lib bcrypt.lib crypt32.lib avrt.lib winmm.lib mfplat.lib mfuuid.lib mmdevapi.lib "%SRC%\libs\rnnoise\rnnoise.lib"

link /NOLOGO %LFLAGS% /MAP:"%BIN%\IMDiscord.map" /NODEFAULTLIB /ENTRY:im_entry /SUBSYSTEM:WINDOWS /MACHINE:X64 /STACK:0x200000,0x200000 /INCREMENTAL:NO /MANIFEST:NO /OUT:"%BIN%\IMDiscord.exe" @"%RSP%" %LIBS%
if errorlevel 1 goto :fail

echo.
echo [build] ok -^> %BIN%\IMDiscord.exe
exit /b 0

:fail
echo.
echo [build] FAILED
exit /b 1
