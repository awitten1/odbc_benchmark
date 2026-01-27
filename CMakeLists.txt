cmake_minimum_required(VERSION 3.24)

project(odbc_benchmark LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_executable(main main.cpp)

find_package(PkgConfig REQUIRED)
pkg_check_modules(ODBC REQUIRED odbc)
target_link_libraries(main PRIVATE ${ODBC_LIBRARIES})
target_include_directories(main PUBLIC ${ODBC_INCLUDE_DIRS})
target_compile_options(main PUBLIC ${ODBC_CFLAGS_OTHER})

pkg_check_modules(LIBPQ REQUIRED libpq)
target_link_libraries(main PRIVATE ${LIBPQ_LIBRARIES})
target_include_directories(main PUBLIC ${LIBPQ_INCLUDE_DIRS})
target_compile_options(main PUBLIC ${LIBPQ_CFLAGS_OTHER})

set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} -O3 -march=native")
install(TARGETS main)