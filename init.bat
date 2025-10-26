@echo off
setlocal
cd %~dp0
echo === Conan install Debug/Release ===
conan install . -of build/Debug   -s build_type=Debug   -g CMakeDeps -g CMakeToolchain --build=missing
conan install . -of build/Release -s build_type=Release -g CMakeDeps -g CMakeToolchain --build=missing
echo === Configure CMake (VS 2022) ===
cmake -B build -S . -G "Visual Studio 17 2022" -DCMAKE_TOOLCHAIN_FILE=build/Release/generators/conan_toolchain.cmake
echo Done.  Open build\fit2srt.sln in Visual Studio.
endlocal