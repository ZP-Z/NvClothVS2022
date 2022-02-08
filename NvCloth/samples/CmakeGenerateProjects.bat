rem @echo off

CD /D %~dp0
@echo off


REM Install cmake using packman
REM set PACKMAN=call ../scripts/packman/packman.cmd
REM %PACKMAN% pull -p windows "../scripts/packman/packages/cmake.packman.xml"
REM IF %ERRORLEVEL% NEQ 0 (
REM     set EXIT_CODE=%ERRORLEVEL%
REM     goto End
REM )
REM set CMAKE=%PM_cmake_PATH%/bin/cmake.exe


set USE_CUDA=0
set USE_DX11=1
IF %1. NEQ . (set USE_CUDA=%1)
IF %2. NEQ . (set USE_DX11=%2)

set CMAKE=cmake

REM Make sure the various variables that we need are set

call "../scripts/locate_cuda.bat" CUDA_PATH_
echo CUDA_PATH_ = %CUDA_PATH_%


set GW_DEPS_ROOT=%~dp0..\..\


IF NOT DEFINED GW_DEPS_ROOT GOTO GW_DEPS_ROOT_UNDEFINED

set PX_OUTPUT_ROOT=%~dp0

set OUTPUT_ROOT=%~dp0
set SAMPLES_ROOT_DIR=%~dp0

set VS_NAME=vc17win64-cmake
set BIN_DIR=bin\%VS_NAME%
set LIB_DIR=lib\%VS_NAME%
set COMPILER_DIR=compiler\%VS_NAME%\

set CMAKE_ARGS=-Ax64 -DNV_CLOTH_ENABLE_CUDA=%USE_CUDA% -DNV_CLOTH_ENABLE_DX11=%USE_DX11% -DTARGET_BUILD_PLATFORM=windows -DSTATIC_WINCRT=0 -DBL_DLL_OUTPUT_DIR=%OUTPUT_ROOT%\%BIN_DIR% -DBL_LIB_OUTPUT_DIR=%OUTPUT_ROOT%\%LIB_DIR% -DBL_EXE_OUTPUT_DIR=%OUTPUT_ROOT%\%BIN_DIR%

IF "%USE_CUDA%" NEQ "0" (set CMAKE_ARGS=-DCUDA_TOOLKIT_ROOT_DIR="%CUDA_PATH_%" %CMAKE_ARGS%)

REM Generate projects here

rmdir /s /q %COMPILER_DIR%
mkdir %COMPILER_DIR%
pushd %COMPILER_DIR%

@echo %CMAKE_ARGS%

%CMAKE% ..\.. -G "Visual Studio 17 2022" %CMAKE_ARGS%

popd

GOTO :End

:End
pause