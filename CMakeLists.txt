cmake_minimum_required(VERSION 3.26)

if (CMAKE_CXX_STANDARD LESS 20)
  message(FATAL_ERROR "At least C++20 required but have ${CMAKE_CXX_STANDARD}")
endif()

project(CppInAction
        DESCRIPTION  "Contemporary C++ in Action demo project"
        HOMEPAGE_URL "https://github.com/DanielaE/CppInAction"
        LANGUAGES CXX
)

if (CMAKE_VERSION VERSION_LESS 3.28)
  if (CMAKE_VERSION VERSION_GREATER_EQUAL 3.27)
    set(CMAKE_EXPERIMENTAL_CXX_MODULE_CMAKE_API "aa1f7df0-828a-4fcd-9afc-2dc80491aca7")
  else ()
    set(CMAKE_EXPERIMENTAL_CXX_MODULE_CMAKE_API "2182bf5c-ef0d-489a-91da-49dbc3090d2a")
  endif()
  set(CMAKE_EXPERIMENTAL_CXX_MODULE_DYNDEP 1)
else()
  cmake_policy(VERSION 3.28)
endif()
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 23)

add_subdirectory(argparse)
add_subdirectory(asio)
add_subdirectory(libav)
add_subdirectory(SDL)
add_subdirectory(stl)
add_subdirectory(Demo-App)
