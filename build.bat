@echo off
echo ==========================================
echo   Endfield SynchroFocus Build Script
echo ==========================================
echo.

:: Set up MSVC environment
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul

where cl >nul 2>nul
if %errorlevel% neq 0 (
    echo [ERROR] cl.exe not found!
    echo Please run this from "x64 Native Tools Command Prompt for VS"
    echo Or run: "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
    pause
    exit /b 1
)

echo [OK] MSVC compiler found
echo.

if not exist bin mkdir bin

:: Build synchro_focus.dll
echo [1/3] Compiling version resource ...
rc /nologo /fo bin\version.res src\version.rc

echo [2/3] Building synchro_focus.dll ...
cl /nologo /O2 /MD /LD /EHsc /std:c++17 ^
    /I"deps\minhook_lib\include" ^
    src\synchro_focus.cpp ^
    "deps\minhook_lib\lib\libMinHook.x64.lib" ^
    bin\version.res ^
    /Fe"bin\synchro_focus.dll" ^
    /link /DLL user32.lib

if %errorlevel% neq 0 (
    echo [ERROR] synchro_focus.dll build failed!
    pause
    exit /b 1
)
echo [OK] synchro_focus.dll built successfully
echo.

:: Build d3dcompiler_47.dll (proxy loader)
echo [3/3] Building d3dcompiler_47.dll (proxy loader) ...
cl /nologo /O2 /MD /LD /EHsc /std:c++17 ^
    src\proxy_d3dcompiler.cpp ^
    /Fe"bin\d3dcompiler_47.dll" ^
    /link /DLL

if %errorlevel% neq 0 (
    echo [ERROR] d3dcompiler_47.dll build failed!
    pause
    exit /b 1
)
echo [OK] d3dcompiler_47.dll built successfully
echo.

:: Clean up intermediate files
del /q synchro_focus.obj 2>nul
del /q synchro_focus.exp 2>nul
del /q synchro_focus.lib 2>nul
del /q proxy_d3dcompiler.obj 2>nul
del /q proxy_d3dcompiler.exp 2>nul
del /q proxy_d3dcompiler.lib 2>nul

echo ==========================================
echo   Build Complete!
echo ==========================================
echo.
echo Output files in bin\:
echo   - synchro_focus.dll       (synchro focus payload)
echo   - d3dcompiler_47.dll    (proxy loader)
echo.
pause
