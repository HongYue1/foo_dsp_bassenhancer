@echo off
rem Package foo_dsp_bassenhancer as a .fb2k-component for foobar2000 v2:
rem   foo_dsp_bassenhancer.dll        x86 (32-bit) build, package root
rem   x64\foo_dsp_bassenhancer.dll    x64 build
rem The LGPL text ships alongside, as the licence requires.
setlocal
cd /d "%~dp0"

if exist "x64\Release\foo_dsp_bassenhancer.dll" del "x64\Release\foo_dsp_bassenhancer.dll"
if exist "Win32\Release\foo_dsp_bassenhancer.dll" del "Win32\Release\foo_dsp_bassenhancer.dll"
if exist build.log del build.log
set NOCLEANLOG=1
call build.bat Release x64
call build.bat Release Win32
if not exist "x64\Release\foo_dsp_bassenhancer.dll" (
  echo FAILED: no x64 DLL - see build.log
  exit /b 1
)
if not exist "Win32\Release\foo_dsp_bassenhancer.dll" (
  echo FAILED: no x86 DLL - see build.log
  exit /b 1
)

if exist dist rmdir /s /q dist
mkdir dist\stage\x64
mkdir dist\symbols
copy /y "Win32\Release\foo_dsp_bassenhancer.dll" "dist\stage\foo_dsp_bassenhancer.dll" >nul
copy /y "x64\Release\foo_dsp_bassenhancer.dll" "dist\stage\x64\foo_dsp_bassenhancer.dll" >nul
copy /y "Win32\Release\foo_dsp_bassenhancer.pdb" "dist\symbols\foo_dsp_bassenhancer-x86.pdb" >nul
copy /y "x64\Release\foo_dsp_bassenhancer.pdb" "dist\symbols\foo_dsp_bassenhancer-x64.pdb" >nul
copy /y "LICENSE" "dist\stage\LICENSE-LGPL-2.1.txt" >nul

set SEVENZIP=C:\Program Files\7-Zip\7z.exe
if not exist "%SEVENZIP%" (
  echo FAILED: 7z.exe not found at "%SEVENZIP%"
  exit /b 1
)
pushd dist\stage
"%SEVENZIP%" a -tzip -bso0 -bsp0 "..\foo_dsp_bassenhancer.fb2k-component" * >nul
set ZIPERR=%ERRORLEVEL%
popd
if not "%ZIPERR%"=="0" (
  echo FAILED: 7z exited with %ZIPERR%
  exit /b 1
)
rmdir /s /q dist\stage

echo Packaged dist\foo_dsp_bassenhancer.fb2k-component (x86 + x64); symbols in dist\symbols
