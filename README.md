NvCloth 1.1.6
===========

Introduction
------------

NvCloth is a library that provides low level access to a cloth solver designed for realtime interactive applications.

Features:
* Fast and robust cloth simulation suitable for games
* Collision detection and response suitable for animated characters
* Low level interface with little overhead and easy integration

This version is a customized version for building in Visual Studio 2022.

Build
-------------
- install visual studio 2022 
- install CUDA 11.6 (or other version, you should modify the CUDA version in ./NvCloth/scripts/locate_cuda.bat)
- run the following script
```
./NvCloth/samples/CmakeGenerateProjects.bat
```
- check visual studio project in NvCloth/samples/compiler/vc17win64-cmake folder
- build in Visual Studio 2022, set `SampleBase` as StartUp project, debug

Documentation
-------------

See ./NvCloth/ReleaseNotes.txt for changes and platform support.
See ./NvCloth/docs/documentation/index.html for the release notes, API users guide and compiling instructions.
See ./NvCloth/docs/doxy/index.html for the api documentation.

PhysX / PxShared compatibility
-----------------------------------
Note that 1.1.6 is compatible with the same version of PxShared shipped with PhysX 4.0.
Please use 1.1.5 if you are compiling it together with PhysX 3.4.
