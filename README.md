NvCloth 1.1.6 from [NVIDIAGameWorks/NvCloth](https://github.com/NVIDIAGameWorks/NvCloth)

Building with Visual Studio 2022
------------
- install visual studio 2022 
- install CUDA 12.8 (or other version, you should modify the CUDA version in ./NvCloth/scripts/locate_cuda.bat)
- run the following script
```
./NvCloth/samples/CmakeGenerateProjects.bat
```
- check visual studio project in NvCloth/samples/compiler/vc17win64-cmake folder
- build in Visual Studio 2022, set `SampleBase` as StartUp project, debug

Note
---------------
1. CUDA version compatibility​: like `nvcc fatal: Unsupported GPU architecture`, verify the compute capability flags in `*.cmake` files. Locate `-gencode arch` entries and adjust them based on your CUDA version's supported architectures. For instance, CUDA 12.8 supports architectures `compute_70` to `compute_90`.

2. CUDA warnings would being treated as errors during compiling, add `/wdxxxx` flag to `CMAKE_CXX_FLAGS`
