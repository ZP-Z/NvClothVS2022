@echo off

%1 mshta vbscript:CreateObject("Shell.Application").ShellExecute("cmd.exe","/c %~s0 ::","","runas",1)(window.close)&&exit

CD /D %~dp0

set USE_CUDA=1
set USE_DX11=1

set CMAKE=cmake

REM Make sure the various variables that we need are set

setx GW_DEPS_ROOT %~dp0../../
set GW_DEPS_ROOT=%~dp0../../
echo GW_DEPS_ROOT = %GW_DEPS_ROOT%

call "../scripts/locate_cuda.bat" CUDA_PATH_
echo CUDA_PATH_ = %CUDA_PATH_%

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