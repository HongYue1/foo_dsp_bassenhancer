@echo off
rem Build foo_dsp_bassenhancer. Usage: build.bat [Release|Debug] [x64|Win32]
rem Read results from build.log, not stdout.
setlocal
cd /d "%~dp0"
set CFG=%1
if "%CFG%"=="" set CFG=Release
if /I "%CFG%"=="Release" (set SDKCFG=Release-Static) else (set SDKCFG=Debug)
set PLAT=%2
if "%PLAT%"=="" set PLAT=x64

call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul

if "%NOCLEANLOG%"=="" if exist build.log del build.log
set SDK=..\SDK-2026-09-17
set MSB=msbuild /nologo /m /v:minimal /p:Platform=%PLAT%
set LOG=/fileLogger "/flp:logfile=build.log;verbosity=normal;append"

for %%P in (
  "%SDK%\pfc\pfc.vcxproj"
  "%SDK%\foobar2000\SDK\foobar2000_SDK.vcxproj"
  "%SDK%\foobar2000\helpers\foobar2000_sdk_helpers.vcxproj"
  "%SDK%\libPPUI\libPPUI.vcxproj"
  "%SDK%\foobar2000\foobar2000_component_client\foobar2000_component_client.vcxproj"
) do (
  %MSB% %%P /p:Configuration=%SDKCFG% %LOG%
  if errorlevel 1 (
    echo FAILED building %%P - see build.log
    exit /b 1
  )
)

%MSB% "foo_dsp_bassenhancer.vcxproj" /p:Configuration=%CFG% %LOG%
echo EXITCODE=%ERRORLEVEL%
