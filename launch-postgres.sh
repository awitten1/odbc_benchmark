#!/bin/bash

#
# After running this script connect to postgres with `psql "host=localhost port=5432 dbname=postgres"`.
#

set -eux

if [ $# -lt 1 ]; then
    echo "./launch-postgres <start|stop>"
    exit
fi

dir=$(dirname $(realpath $0))
pushd $dir

mkdir -p data_dir

pg_versions=(/usr/lib/postgresql/*)

pg_version=${pg_versions[-1]}

set_pg_option() {
    key=$1
    value=$2
    pg_conf="$3/postgresql.conf"

    sed -i "/$1/d" $pg_conf
    echo "$key = $value" >> $pg_conf
}


if [[ $1 = "start" || $1 = "init" ]]; then
    if [ ! -f data_dir/PG_VERSION ]; then
        $pg_version/bin/pg_ctl init -D data_dir
        set_pg_option 'unix_socket_directories' "'/tmp'" "data_dir"
        set_pg_option 'logging_collector' "true" "data_dir"
        set_pg_option 'log_destination' "'csvlog,jsonlog'" "data_dir"
        set_pg_option 'log_directory' "'logs'" "data_dir"
    fi
    if [ $1 = "start" ]; then
        $pg_version/bin/pg_ctl start -D data_dir
    fi
elif [ $1 = "stop" ]; then
    $pg_version/bin/pg_ctl stop -D data_dir -m fast
fi

popd
