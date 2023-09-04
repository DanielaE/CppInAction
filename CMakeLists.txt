cmake_minimum_required(VERSION 3.26)

if (CMAKE_CXX_STANDARD LESS 20)
  message(FATAL_ERROR "At least C++20 required but have ${CMAKE_CXX_STANDARD}")
endif()

project(CppInAction
        DESCRIPTION  "Contemporary C++ in Action demo project"
        HOMEPAGE_URL "https://github.com/DanielaE/CppInAction"
        LANGUAGES CXX
)

if (CMAKE_VERSION VERSION_GREATER_EQUAL 3.27)
  set(CMAKE_EXPERIMENTAL_CXX_MODULE_CMAKE_API "aa1f7df0-828a-4fcd-9afc-2dc80491aca7")
else ()
  set(CMAKE_EXPERIMENTAL_CXX_MODULE_CMAKE_API "2182bf5c-ef0d-489a-91da-49dbc3090d2a")
endif()
set(CMAKE_EXPERIMENTAL_CXX_MODULE_DYNDEP 1)
set(CMAKE_CXX_EXTENSIONS OFF)

if (MSVC)
  set(CMAKE_CXX_STANDARD 23)
elseif (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  set(CMAKE_CXX_STANDARD 23)
endif()

add_subdirectory(argparse)
add_subdirectory(asio)
add_subdirectory(libav)
add_subdirectory(SDL)
add_subdirectory(stl)
add_subdirectory(Demo-App)

add_executable(library-bmis library-bmis.cpp)
target_compile_features(library-bmis PUBLIC cxx_std_20)
target_link_libraries(library-bmis PRIVATE argparse asio libav sdl std)

