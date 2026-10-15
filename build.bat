@echo off
setlocal
cd /d "%~dp0"
if not "%~1"=="" (
    echo Usage: build.bat
    exit /b 2
)
set "SYNCHROFOCUS_SDK_DIR=%~dp0deps\applepie_sdk"
for %%H in (applepie.h applepie_client.h applepie_native.h applepie_seh.h applepie_pointer_scope.h) do (
    if not exist "%SYNCHROFOCUS_SDK_DIR%\%%H" (
        echo [ERROR] Missing bundled SDK header: deps\applepie_sdk\%%H
        exit /b 1
    )
)
where cl >nul 2>nul
if errorlevel 1 (
    if not exist "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" (
        echo [ERROR] Use an x64 Developer Command Prompt or install MSVC.
        exit /b 1
    )
    for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do call "%%i\VC\Auxiliary\Build\vcvars64.bat" >nul
)
where cl >nul 2>nul
if errorlevel 1 exit /b 1
if not exist bin mkdir bin
echo [SDK] %SYNCHROFOCUS_SDK_DIR%
rc /nologo /fo bin\version.res src\version.rc
if errorlevel 1 exit /b 1
cl /nologo /utf-8 /O2 /MT /LD /EHsc /std:c++17 /W4 /wd4505 /I"%SYNCHROFOCUS_SDK_DIR%" ^
    src\synchro_focus.cpp bin\version.res /Fo:bin\synchro_focus.obj /Fe:bin\synchro_focus.dll ^
    /link /INCREMENTAL:NO /OPT:NOICF /MAP:bin\synchro_focus.map user32.lib
if errorlevel 1 exit /b 1
echo [OK] Built bin\synchro_focus.dll.
exit /b 0
