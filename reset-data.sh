#!/bin/bash

set -euo pipefail

HOST="localhost"
PORT="5432"
DBNAME="postgres"
DBUSER="${USER:-}"
SCHEMA="bench"

while [ $# -gt 0 ]; do
    case "$1" in
        --host) HOST="$2"; shift 2 ;;
        --port) PORT="$2"; shift 2 ;;
        --dbname) DBNAME="$2"; shift 2 ;;
        --user) DBUSER="$2"; shift 2 ;;
        --schema) SCHEMA="$2"; shift 2 ;;
        *)
            echo "Unknown arg: $1"
            echo "Usage: ./reset-data.sh [--host H] [--port P] [--dbname DB] [--user U] [--schema S]"
            exit 1
            ;;
    esac
done

PSQL="./deps_install/bin/psql"
if [ ! -x "$PSQL" ]; then
    PSQL="psql"
fi
if [ -z "${DBUSER}" ]; then
    DBUSER="$(whoami)"
fi

CONN="host=${HOST} port=${PORT} dbname=${DBNAME}"
if [ -n "${DBUSER}" ]; then
    CONN="${CONN} user=${DBUSER}"
fi

"$PSQL" "${CONN}" -v ON_ERROR_STOP=1 -c "DROP SCHEMA IF EXISTS ${SCHEMA} CASCADE;"
