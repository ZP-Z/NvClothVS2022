NvCloth 1.1.6 de [NVIDIAGameWorks/NvCloth](https://github.com/NVIDIAGameWorks/NvCloth)

Construir con Visual Studio 2022
------------
- Instala Visual Studio 2022.
- Instala CUDA 12.8 (o cualquier otra versión; tendrás que modificar la versión de CUDA en el archivo ./NvCloth/scripts/locate_cuda.bat).
- Ejecuta el siguiente script:
```
./NvCloth/samples/CmakeGenerateProjects.bat
```
- Verifica el proyecto de Visual Studio en la carpeta NvCloth/samples/compiler/vc17win64-cmake.
- Compila en Visual Studio 2022, establece `SampleBase` como proyecto de inicio y ejecútalo en modo de depuración.

Nota
---------------
1. Compatibilidad de versiones de CUDA: si encuentras un error como `nvcc fatal: Unsupported GPU architecture`, verifica las banderas de capacidad de cálculo en los archivos `*.cmake`. Busca las entradas `-gencode arch` y ajústalas según las arquitecturas compatibles con tu versión de CUDA. Por ejemplo, CUDA 12.8 es compatible con arquitecturas desde `compute_70` hasta `compute_90`.

2. Las advertencias de CUDA se tratarán como errores durante la compilación, por lo que debes agregar la bandera `/wdxxxx` a `CMAKE_CXX_FLAGS`.
