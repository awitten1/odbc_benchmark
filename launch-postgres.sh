#!/bin/bash

#
# For this to work, I had to run sudo chmod a+w /var/run/postgresql.
# After running this script connect to postgres with `psql -d postgres`.
#

set -eux

if [ $# -lt 1 ]; then
    echo "./launch-postgres <start|stop>"
    exit
fi

dir=$(dirname $(realpath $0))
pushd $dir

mkdir -p data_dir

if [ $1 = "start" ]; then
    if [ ! -f data_dir/PG_VERSION ]; then
        /usr/lib/postgresql/17/bin/pg_ctl init -D data_dir
    fi
    /usr/lib/postgresql/17/bin/pg_ctl start -D data_dir
elif [ $1 = "stop" ]; then
    /usr/lib/postgresql/17/bin/pg_ctl stop -D data_dir -m fast
fi

popd