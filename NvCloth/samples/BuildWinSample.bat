@echo off
@echo "Enable CUDA ? Y/N"

set /p ENABLE_CUDA=

@echo "Enable DX11 ? Y/N"

set /p ENABLE_DX11=

set USE_CUDA=0
set USE_DX11=0

if "%ENABLE_CUDA%" equ "Y" (set USE_CUDA=1)
if "%ENABLE_CUDA%" equ "y" (set USE_CUDA=1)

if "%ENABLE_DX11%" equ "Y" (set USE_DX11=1)
if "%ENABLE_DX11%" equ "y" (set USE_DX11=1)

call CmakeGenerateProjects.bat %USE_CUDA% %USE_DX11%

pause