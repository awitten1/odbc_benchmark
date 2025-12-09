cmake_minimum_required(VERSION 3.24)

project(odbc_benchmark LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_executable(main main.cpp)

find_package(PkgConfig REQUIRED)
pkg_check_modules(ODBC REQUIRED odbc)
target_link_libraries(main ${ODBC_LIBRARIES})
target_include_directories(main PUBLIC ${ODBC_INCLUDE_DIRS})
target_compile_options(main PUBLIC ${ODBC_CFLAGS_OTHER})