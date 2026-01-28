#!/bin/bash

set -eux

mkdir -p deps_install
(cd deps/postgres && CFLAGS="-O3 -march=native -g" CXXFLAGS="-O3 -march=native -g" ./configure --prefix=$(realpath ../../deps_install/))
(cd deps/postgres && make -j6 && make install)

mkdir -p install

cmake -S deps/arrow-nanoarrow -B nanoarrow_build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_INSTALL_PREFIX=$(realpath ./deps_install)
cmake --build nanoarrow_build -j4
cmake --install nanoarrow_build

cmake -S deps/arrow-adbc/c -B arrowadbc_build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DADBC_DRIVER_POSTGRESQL=ON \
    -DCMAKE_INSTALL_PREFIX=$(realpath ./deps_install) -DCMAKE_PREFIX_PATH=$(realpath ./deps_install)
cmake --build arrowadbc_build -j4
cmake --install arrowadbc_build

cmake -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_PREFIX_PATH=$(realpath ./deps_install) \
    -DCMAKE_INSTALL_RPATH=./deps_install/lib \
    -DCMAKE_INSTALL_PREFIX=$(realpath ./install)

cmake --build build
cmake --install build

