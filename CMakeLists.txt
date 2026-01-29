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

find_package(AdbcDriverPostgreSQL CONFIG REQUIRED)
target_link_libraries(main PUBLIC AdbcDriverPostgreSQL::adbc_driver_postgresql_shared)

find_path(ADBC_INCLUDE_DIR arrow-adbc/adbc.h PATHS ${CMAKE_PREFIX_PATH} PATH_SUFFIXES include)
if(ADBC_INCLUDE_DIR)
    target_include_directories(main PRIVATE ${ADBC_INCLUDE_DIR})
endif()

set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} -O3 -march=native")
install(TARGETS main)

find_package(benchmark CONFIG REQUIRED)
add_executable(gbench bench.cpp)
target_link_libraries(gbench PRIVATE benchmark::benchmark ${ODBC_LIBRARIES})
target_include_directories(gbench PUBLIC ${ODBC_INCLUDE_DIRS})
target_compile_options(gbench PUBLIC ${ODBC_CFLAGS_OTHER})

target_link_libraries(gbench PRIVATE ${LIBPQ_LIBRARIES})
target_include_directories(gbench PUBLIC ${LIBPQ_INCLUDE_DIRS})
target_compile_options(gbench PUBLIC ${LIBPQ_CFLAGS_OTHER})

target_link_libraries(gbench PUBLIC AdbcDriverPostgreSQL::adbc_driver_postgresql_shared)
if(ADBC_INCLUDE_DIR)
    target_include_directories(gbench PRIVATE ${ADBC_INCLUDE_DIR})
endif()
