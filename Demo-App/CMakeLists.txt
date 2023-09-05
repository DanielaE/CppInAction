cmake_minimum_required(VERSION 3.26)

if (CMAKE_CXX_STANDARD LESS 20)
  message(FATAL_ERROR "At least C++20 required but have ${CMAKE_CXX_STANDARD}")
endif()

project(std
        DESCRIPTION "Contemporary C++ in Action demo app"
        LANGUAGES CXX
)

if (CMAKE_VERSION VERSION_GREATER_EQUAL 3.27)
  set(CMAKE_EXPERIMENTAL_CXX_MODULE_CMAKE_API "aa1f7df0-828a-4fcd-9afc-2dc80491aca7")
else ()
  set(CMAKE_EXPERIMENTAL_CXX_MODULE_CMAKE_API "2182bf5c-ef0d-489a-91da-49dbc3090d2a")
endif()
set(CMAKE_EXPERIMENTAL_CXX_MODULE_DYNDEP 1)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CXX_STANDARD_REQUIRED ON)

add_executable(demo )

if (MSVC)
# I do mean C++23
  target_compile_options(demo PUBLIC /utf-8 /Zc:__cplusplus /Zc:throwingNew /Zc:inline /Zc:externConstexpr /Zc:templateScope /Zc:checkGwOdr /Zc:enumTypes)
  target_compile_features(demo PUBLIC cxx_std_23)
elseif (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  set(CMAKE_CXX_STANDARD 20)
  target_compile_features(demo PUBLIC cxx_std_23)
# I do mean C++20
  target_compile_options(demo PUBLIC -fsized-deallocation -faligned-allocation)
else()
# these are the desired defaults
  target_compile_features(demo PUBLIC cxx_std_23)
endif()

set(module-if
    caboodle.ixx client.ixx events.ixx executor.ixx gui.ixx net.ixx
    server.ixx video.ixx videodecoder.ixx videoframe.ixx)
set(module-internal-partitions videodecoder.cpp)
set(agnostic-module-impl
    caboodle-program-arguments.cpp gui.cpp net.cpp)
set(Posix-module-impl caboodle-posix.cpp)
set(Windows-module-impl caboodle-windows.cpp)
set(header-units c_resource.hpp)

target_sources(demo
  PRIVATE main.cpp ${agnostic-module-impl}
  PRIVATE
    FILE_SET modules TYPE CXX_MODULES
      FILES ${module-if} ${module-internal-partitions}
)
if (MSVC)
  target_sources(demo PRIVATE ${Windows-module-impl})
  target_sources(demo PRIVATE Demo-App.xml)
else()
  target_sources(demo PRIVATE ${Posix-module-impl})
endif()

target_link_libraries(demo PRIVATE argparse asio libav sdl std)
