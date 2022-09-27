[![CC BY-SA 4.0][cc-by-sa-shield]][cc-by-sa]

# CppInAction

Demo Code for presentation "Contemporary C++ In Action"

This code does not compile without the missing pieces (the precompiled modules mentioned in the code) because it is for educational purposes only. Besides that it is complete. 

The missing pieces are:
 1) Asio, preferably non-Boost [Asio](https://think-async.com/Asio)
 2) libav [FFmpeg](https://ffmpeg.org/download.html)
 3) SDL2 [SDL](https://www.libsdl.org/download-2.0.php)
 4) Boost.Program_options [Boost](https://www.boost.org/doc/libs/1_79_0/doc/html/program_options.html)

I have forked
 1) [Asio, branch 'module'](https://github.com/DanielaE/asio/tree/module)
 2) [SDL2, branch 'module'](https://github.com/DanielaE/SDL/tree/module)
 3) [Microsoft's standard library, branch 'my-stl'](https://github.com/DanielaE/STL/tree/my-stl)

which contain the necessary changes to compile Asio and SDL as modules. My take on the STL adds Casey Carter's current implementation of `<generator>`, plus my implementation of `<print>` and partial implementation of C++26's *explicit lifetime management* [P2590](https://wg21.link/P2590) on top of the latest work in Microsoft's open source standard library.

There is also a [minimum set of sources](https://github.com/DanielaE/libav.module/tree/main) from FFmpeg v5.0.1 to compile module `libav`. You need to provide the necessary link libraries yourself if you want to build the executable.

I will **not** provide the module implementation of Boost.Program_options. Please bring your own favourite command line processor and modify module `the.whole.caboodle` accordingly.

### License
This work is licensed under a
[Creative Commons Attribution-ShareAlike 4.0 International License][cc-by-sa].

[![CC BY-SA 4.0][cc-by-sa-image]][cc-by-sa]

[cc-by-sa]: http://creativecommons.org/licenses/by-sa/4.0/
[cc-by-sa-image]: https://licensebuttons.net/l/by-sa/4.0/88x31.png
[cc-by-sa-shield]: https://img.shields.io/badge/License-CC%20BY--SA%204.0-lightgrey.svg
