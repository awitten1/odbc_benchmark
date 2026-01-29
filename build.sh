#!/bin/bash

set -eux

DIR=$(pwd)

INSTALL_DIR="$DIR/deps_install"
BUILD_DIR="$DIR/deps_build"
SOURCE_DIR="$DIR/deps_src"

install_system_deps() {
    sudo apt install -y curl ca-certificates
    sudo install -d /usr/share/postgresql-common/pgdg
    sudo curl -o /usr/share/postgresql-common/pgdg/apt.postgresql.org.asc \
        --fail https://www.postgresql.org/media/keys/ACCC4CF8.asc

    . /etc/os-release
    sudo sh -c "echo 'deb [signed-by=/usr/share/postgresql-common/pgdg/apt.postgresql.org.asc] https://apt.postgresql.org/pub/repos/apt \
        $VERSION_CODENAME-pgdg main' > /etc/apt/sources.list.d/pgdg.list"

    sudo apt update
    sudo apt install -y unixodbc unixodbc-dev odbc-postgresql postgresql-18
}

install_libpfm() {
    mkdir -p "$SOURCE_DIR"
    pushd "$SOURCE_DIR"
    if [ ! -d "libpfm4" ]; then
        git clone --branch v4.13.0 https://github.com/wcohen/libpfm4.git
    fi
    pushd libpfm4
    make PREFIX="$INSTALL_DIR" install
    popd
    popd
}

install_google_benchmark() {
    local gbench_version=v1.9.4

    mkdir -p "$SOURCE_DIR"
    mkdir -p "$BUILD_DIR/benchmark"

    pushd "$SOURCE_DIR"
    if [ ! -d "benchmark" ]; then
        git clone --branch ${gbench_version} https://github.com/google/benchmark.git
    fi
    popd

    local enable_libpfm=OFF
    if [ $(uname -s) = 'Linux' ]; then
        enable_libpfm=ON
        install_libpfm
    fi

    cmake -B "$BUILD_DIR/benchmark" -S "$SOURCE_DIR/benchmark" -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" \
        -DCMAKE_BUILD_TYPE=Release -DBENCHMARK_DOWNLOAD_DEPENDENCIES=ON -G Ninja \
        -DBENCHMARK_ENABLE_LIBPFM="${enable_libpfm}" -DCMAKE_PREFIX_PATH="$INSTALL_DIR" \
        -DBENCHMARK_ENABLE_GTEST_TESTS=OFF

    cmake --build "$BUILD_DIR/benchmark" -j8
    cmake --install "$BUILD_DIR/benchmark"
}

build_project() {
    mkdir -p "$INSTALL_DIR"
    (cd deps/postgres && CFLAGS="-O3 -march=native -g" CXXFLAGS="-O3 -march=native -g" \
        ./configure --prefix="$INSTALL_DIR")
    (cd deps/postgres && make -j6 && make install)

    mkdir -p install

    cmake -S deps/arrow-nanoarrow -B nanoarrow_build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
        -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR"
    cmake --build nanoarrow_build -j4
    cmake --install nanoarrow_build

    cmake -S deps/arrow-adbc/c -B arrowadbc_build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
        -DADBC_DRIVER_MANAGER=ON \
        -DADBC_DRIVER_POSTGRESQL=ON \
        -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" -DCMAKE_PREFIX_PATH="$INSTALL_DIR"
    cmake --build arrowadbc_build -j4
    cmake --install arrowadbc_build

    cmake -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo \
        -DCMAKE_PREFIX_PATH="$INSTALL_DIR" \
        -DCMAKE_INSTALL_RPATH="./deps_install/lib" \
        -DCMAKE_INSTALL_PREFIX="$DIR/install"

    cmake --build build
    cmake --install build
}

if [ $# -eq 0 ]; then
    install_system_deps
    install_google_benchmark
    build_project
else
    for arg in "$@"; do
        case "$arg" in
            --system-deps) install_system_deps ;;
            --gbench) install_google_benchmark ;;
            --build) build_project ;;
            *)
                echo "Unknown arg: $arg"
                echo "Usage: ./build.sh [--system-deps] [--gbench] [--build]"
                exit 1
                ;;
        esac
    done
fi
