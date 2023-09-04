[![CC BY-SA 4.0][cc-by-sa-shield]][cc-by-sa]

# CppInAction

Demo Code for presentation "Contemporary C++ In Action"

This code does not compile without the missing pieces (the precompiled modules mentioned in the code) because it is for educational purposes only. Besides that it is complete. 

The missing pieces are:
 1) Asio, preferably non-Boost [Asio](https://think-async.com/Asio)
 2) libav [FFmpeg](https://ffmpeg.org/download.html)
 3) SDL2 [SDL](https://www.libsdl.org/download-2.0.php)
 4) argparse [argparse](https://github.com/p-ranav/argparse)
 5) modularized standard library

I have forked
 1) [Asio, branch 'module'](https://github.com/DanielaE/asio/tree/module)
 2) [SDL2, branch 'module'](https://github.com/DanielaE/SDL/tree/module)
 3) [argparse, branch 'module'](https://github.com/DanielaE/argparse/tree/module)
 4) [libav, branch 'module'](https://github.com/DanielaE/libav.module/tree/module)
 
 plus

 5) a modularized standard library with a polyfill for the missing, not yet implemented parts: [std.module](https://github.com/DanielaE/std.module/tree/module)

which contain the necessary changes to compile Asio, SDL, libav, and argparse as modules. My take on the C++ standard library module adds Casey Carter's current implementation of `<generator>`, plus my implementation of `<print>` and a partial implementation of C++26's *explicit lifetime management* [P2590](https://wg21.link/P2590) on top of the standard library that comes with the compiler toolset of your choice.

There is also a [minimum set of sources](https://github.com/DanielaE/libav.module/tree/main) from FFmpeg v6.0 to compile module `libav`. You need to provide the necessary link libraries yourself if you want to build the executable.

The videos of the keynote presentations are here:
 - [CppCon 2022](https://youtu.be/yUIFdL3D0Vk)
 - [Meeting C++ 2022](https://youtu.be/el-xE645Clo)

## Building the app
## Windows
Update 4 or better is highly recommended.
 - open a VS2022 command line window
 - cmake -B bld-msvc -G Ninja -Wno-dev -DCMAKE_CXX_STANDARD=23 --fresh
 - ninja -C bld-msvc

## MSYS2 (UCRT64)
Clang 16.0.2 or better is required. 
 - open a MSYS2 window
 - export CC=clang
 - export CXX=clang++
 - cmake -B bld-clang -G Ninja -Wno-dev -DCMAKE_CXX_STANDARD=23 -DCMAKE_CXX_FLAGS="-stdlib=libc++" --fresh
 - ninja -C bld-clang

## Linux
Clang 16.0.2 or better is required. Clang 17 is recommended.
 - open a terminal window
 - export CC=clang-1x
 - export CXX=clang++-1x
 - cmake -B bld -G Ninja -Wno-dev -DCMAKE_CXX_STANDARD=23 -DCMAKE_CXX_FLAGS="-stdlib=libc++" --fresh
 - ninja -C bld


### License
This work is licensed under a
[Creative Commons Attribution-ShareAlike 4.0 International License][cc-by-sa].

[![CC BY-SA 4.0][cc-by-sa-image]][cc-by-sa]

[cc-by-sa]: http://creativecommons.org/licenses/by-sa/4.0/
[cc-by-sa-image]: https://licensebuttons.net/l/by-sa/4.0/88x31.png
[cc-by-sa-shield]: https://img.shields.io/badge/License-CC%20BY--SA%204.0-lightgrey.svg
