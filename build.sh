#!/bin/bash

set -eux

mkdir -p libpq_install
(cd postgres && CFLAGS="-O3 -march=native -g" CXXFLAGS="-O3 -march=native -g" ./configure --prefix=$(realpath ../libpq_install/))
(cd postgres && make -j6 && make install)

mkdir -p install

cmake -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_PREFIX_PATH=$(realpath ./libpq_install) \
    -DCMAKE_INSTALL_RPATH=./libpq_install/lib \
    -DCMAKE_INSTALL_PREFIX=$(realpath ./install)

cmake --build build
cmake --install build

