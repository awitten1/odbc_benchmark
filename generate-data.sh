#!/bin/bash

set -euo pipefail

HOST="localhost"
PORT="5432"
DBNAME="postgres"
DBUSER="${USER:-}"
SCHEMA="bench"
SCALE=1

while [ $# -gt 0 ]; do
    case "$1" in
        --host) HOST="$2"; shift 2 ;;
        --port) PORT="$2"; shift 2 ;;
        --dbname) DBNAME="$2"; shift 2 ;;
        --user) DBUSER="$2"; shift 2 ;;
        --schema) SCHEMA="$2"; shift 2 ;;
        --scale) SCALE="$2"; shift 2 ;;
        *)
            echo "Unknown arg: $1"
            echo "Usage: ./generate-data.sh [--host H] [--port P] [--dbname DB] [--user U] [--schema S] [--scale N]"
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

TABLES=("region" "nation" "supplier" "part" "partsupp" "customer" "orders" "lineitem" "narrow" "wide")

TABLE_LIST=$(printf "'%s'," "${TABLES[@]}")
TABLE_LIST="${TABLE_LIST%,}"

existing=$("$PSQL" "${CONN}" -At -v ON_ERROR_STOP=1 -c \
    "select count(*) from information_schema.tables where table_schema='${SCHEMA}' and table_name in (${TABLE_LIST});")

if [ "${existing}" = "${#TABLES[@]}" ]; then
    echo "Schema ${SCHEMA} already has all tables; nothing to do."
    exit 0
fi

start_ts=$(date +%s)
log_step() {
    local label="$1"
    local now
    now=$(date +%s)
    echo "[$(date -Iseconds)] ${label} (+$((now - start_ts))s)"
}

table_has_data() {
    local tbl="$1"
    local count
    count=$("$PSQL" "${CONN}" -At -v ON_ERROR_STOP=1 -c \
        "select 1 from ${SCHEMA}.${tbl} limit 1;") || return 1
    [ -n "${count}" ]
}

customer_rows=$((10000 * SCALE))
orders_rows=$((50000 * SCALE))
lineitem_rows=$((200000 * SCALE))
part_rows=$((10000 * SCALE))
supplier_rows=$((1000 * SCALE))
partsupp_rows=$((40000 * SCALE))
narrow_rows=$((50000 * SCALE))
wide_rows=$((20000 * SCALE))

log_step "creating schema/tables"
"$PSQL" "${CONN}" -v ON_ERROR_STOP=1 <<SQL
CREATE SCHEMA IF NOT EXISTS ${SCHEMA};

CREATE TABLE IF NOT EXISTS ${SCHEMA}.region (
    r_regionkey int primary key,
    r_name text not null,
    r_comment text
);

CREATE TABLE IF NOT EXISTS ${SCHEMA}.nation (
    n_nationkey int primary key,
    n_name text not null,
    n_regionkey int not null references ${SCHEMA}.region(r_regionkey),
    n_comment text
);

CREATE TABLE IF NOT EXISTS ${SCHEMA}.supplier (
    s_suppkey int primary key,
    s_name text not null,
    s_address text not null,
    s_nationkey int not null references ${SCHEMA}.nation(n_nationkey),
    s_phone text not null,
    s_acctbal numeric(12,2) not null,
    s_comment text
);

CREATE TABLE IF NOT EXISTS ${SCHEMA}.part (
    p_partkey int primary key,
    p_name text not null,
    p_mfgr text not null,
    p_brand text not null,
    p_type text not null,
    p_size int not null,
    p_container text not null,
    p_retailprice numeric(12,2) not null,
    p_comment text
);

CREATE TABLE IF NOT EXISTS ${SCHEMA}.partsupp (
    ps_partkey int not null references ${SCHEMA}.part(p_partkey),
    ps_suppkey int not null references ${SCHEMA}.supplier(s_suppkey),
    ps_availqty int not null,
    ps_supplycost numeric(12,2) not null,
    ps_comment text,
    primary key (ps_partkey, ps_suppkey)
);

CREATE TABLE IF NOT EXISTS ${SCHEMA}.customer (
    c_custkey int primary key,
    c_name text not null,
    c_address text not null,
    c_nationkey int not null references ${SCHEMA}.nation(n_nationkey),
    c_phone text not null,
    c_acctbal numeric(12,2) not null,
    c_mktsegment text not null,
    c_comment text
);

CREATE TABLE IF NOT EXISTS ${SCHEMA}.orders (
    o_orderkey int primary key,
    o_custkey int not null references ${SCHEMA}.customer(c_custkey),
    o_orderstatus char(1) not null,
    o_totalprice numeric(12,2) not null,
    o_orderdate date not null,
    o_orderpriority text not null,
    o_clerk text not null,
    o_shippriority int not null,
    o_comment text
);

CREATE TABLE IF NOT EXISTS ${SCHEMA}.lineitem (
    l_orderkey int not null references ${SCHEMA}.orders(o_orderkey),
    l_partkey int not null references ${SCHEMA}.part(p_partkey),
    l_suppkey int not null references ${SCHEMA}.supplier(s_suppkey),
    l_linenumber int not null,
    l_quantity numeric(12,2) not null,
    l_extendedprice numeric(12,2) not null,
    l_discount numeric(12,2) not null,
    l_tax numeric(12,2) not null,
    l_returnflag char(1) not null,
    l_linestatus char(1) not null,
    l_shipdate date not null,
    l_commitdate date not null,
    l_receiptdate date not null,
    l_shipinstruct text not null,
    l_shipmode text not null,
    l_comment text,
    primary key (l_orderkey, l_linenumber)
);

CREATE TABLE IF NOT EXISTS ${SCHEMA}.narrow (
    id int primary key,
    v_int int not null,
    v_text text not null,
    v_flag boolean not null
);

CREATE TABLE IF NOT EXISTS ${SCHEMA}.wide (
    id int primary key,
    v_smallint smallint not null,
    v_int int not null,
    v_bigint bigint not null,
    v_numeric numeric(18,4) not null,
    v_real real not null,
    v_double double precision not null,
    v_bool boolean not null,
    v_date date not null,
    v_ts timestamp not null,
    v_text text not null,
    v_jsonb jsonb not null,
    v_bytea bytea not null,
    v_uuid uuid not null,
    v_char char(8) not null,
    v_varchar varchar(64) not null
);
SQL

log_step "inserting region"
if ! table_has_data "region"; then
"$PSQL" "${CONN}" -v ON_ERROR_STOP=1 <<SQL
INSERT INTO ${SCHEMA}.region (r_regionkey, r_name, r_comment)
SELECT i, 'region-' || i, md5(i::text)
FROM generate_series(0, 4) AS s(i)
ON CONFLICT DO NOTHING;
SQL
else
    log_step "region already populated"
fi

log_step "inserting nation"
if ! table_has_data "nation"; then
"$PSQL" "${CONN}" -v ON_ERROR_STOP=1 <<SQL
INSERT INTO ${SCHEMA}.nation (n_nationkey, n_name, n_regionkey, n_comment)
SELECT i, 'nation-' || i, (i % 5), md5(i::text)
FROM generate_series(0, 24) AS s(i)
ON CONFLICT DO NOTHING;
SQL
else
    log_step "nation already populated"
fi

log_step "inserting supplier"
if ! table_has_data "supplier"; then
"$PSQL" "${CONN}" -v ON_ERROR_STOP=1 <<SQL
INSERT INTO ${SCHEMA}.supplier (s_suppkey, s_name, s_address, s_nationkey, s_phone, s_acctbal, s_comment)
SELECT i,
       'supplier-' || i,
       md5(i::text),
       (i % 25),
       '555-' || lpad(i::text, 4, '0'),
       (random() * 10000)::numeric(12,2),
       md5(random()::text)
FROM generate_series(1, ${supplier_rows}) AS s(i)
ON CONFLICT DO NOTHING;
SQL
else
    log_step "supplier already populated"
fi

log_step "inserting part"
if ! table_has_data "part"; then
"$PSQL" "${CONN}" -v ON_ERROR_STOP=1 <<SQL
INSERT INTO ${SCHEMA}.part (p_partkey, p_name, p_mfgr, p_brand, p_type, p_size, p_container, p_retailprice, p_comment)
SELECT i,
       'part-' || i,
       'mfgr-' || (i % 5),
       'brand-' || (i % 40),
       'type-' || (i % 10),
       (i % 50),
       'cont-' || (i % 7),
       (random() * 1000)::numeric(12,2),
       md5(random()::text)
FROM generate_series(1, ${part_rows}) AS s(i)
ON CONFLICT DO NOTHING;
SQL
else
    log_step "part already populated"
fi

log_step "inserting partsupp"
if ! table_has_data "partsupp"; then
"$PSQL" "${CONN}" -v ON_ERROR_STOP=1 <<SQL
INSERT INTO ${SCHEMA}.partsupp (ps_partkey, ps_suppkey, ps_availqty, ps_supplycost, ps_comment)
SELECT (i % ${part_rows}) + 1,
       (i % ${supplier_rows}) + 1,
       (i % 9999),
       (random() * 1000)::numeric(12,2),
       md5(random()::text)
FROM generate_series(1, ${partsupp_rows}) AS s(i)
ON CONFLICT DO NOTHING;
SQL
else
    log_step "partsupp already populated"
fi

log_step "inserting customer"
if ! table_has_data "customer"; then
"$PSQL" "${CONN}" -v ON_ERROR_STOP=1 <<SQL
INSERT INTO ${SCHEMA}.customer (c_custkey, c_name, c_address, c_nationkey, c_phone, c_acctbal, c_mktsegment, c_comment)
SELECT i,
       'customer-' || i,
       md5(i::text),
       (i % 25),
       '555-' || lpad(i::text, 6, '0'),
       (random() * 10000)::numeric(12,2),
       CASE (i % 5)
           WHEN 0 THEN 'AUTOMOBILE'
           WHEN 1 THEN 'BUILDING'
           WHEN 2 THEN 'FURNITURE'
           WHEN 3 THEN 'MACHINERY'
           ELSE 'HOUSEHOLD'
       END,
       md5(random()::text)
FROM generate_series(1, ${customer_rows}) AS s(i)
ON CONFLICT DO NOTHING;
SQL
else
    log_step "customer already populated"
fi

log_step "inserting orders"
if ! table_has_data "orders"; then
"$PSQL" "${CONN}" -v ON_ERROR_STOP=1 <<SQL
INSERT INTO ${SCHEMA}.orders (o_orderkey, o_custkey, o_orderstatus, o_totalprice, o_orderdate,
                              o_orderpriority, o_clerk, o_shippriority, o_comment)
SELECT i,
       (i % ${customer_rows}) + 1,
       CASE (i % 3) WHEN 0 THEN 'O' WHEN 1 THEN 'F' ELSE 'P' END,
       (random() * 10000)::numeric(12,2),
       date '2010-01-01' + (i % 3650),
       CASE (i % 5)
           WHEN 0 THEN '1-URGENT'
           WHEN 1 THEN '2-HIGH'
           WHEN 2 THEN '3-MEDIUM'
           WHEN 3 THEN '4-NOT SPECIFIED'
           ELSE '5-LOW'
       END,
       'clerk-' || (i % 1000),
       (i % 7),
       md5(random()::text)
FROM generate_series(1, ${orders_rows}) AS s(i)
ON CONFLICT DO NOTHING;
SQL
else
    log_step "orders already populated"
fi

log_step "inserting lineitem"
if ! table_has_data "lineitem"; then
"$PSQL" "${CONN}" -v ON_ERROR_STOP=1 <<SQL
INSERT INTO ${SCHEMA}.lineitem (l_orderkey, l_partkey, l_suppkey, l_linenumber, l_quantity,
                                l_extendedprice, l_discount, l_tax, l_returnflag, l_linestatus,
                                l_shipdate, l_commitdate, l_receiptdate, l_shipinstruct, l_shipmode, l_comment)
SELECT (i % ${orders_rows}) + 1,
       (i % ${part_rows}) + 1,
       (i % ${supplier_rows}) + 1,
       (i % 7) + 1,
       (random() * 50)::numeric(12,2),
       (random() * 1000)::numeric(12,2),
       (random() * 0.1)::numeric(12,2),
       (random() * 0.08)::numeric(12,2),
       CASE (i % 3) WHEN 0 THEN 'N' WHEN 1 THEN 'R' ELSE 'A' END,
       CASE (i % 2) WHEN 0 THEN 'O' ELSE 'F' END,
       date '2010-01-01' + (i % 3650),
       date '2010-01-01' + ((i + 3) % 3650),
       date '2010-01-01' + ((i + 6) % 3650),
       'DELIVER IN PERSON',
       CASE (i % 4) WHEN 0 THEN 'AIR' WHEN 1 THEN 'FOB' WHEN 2 THEN 'MAIL' ELSE 'SHIP' END,
       md5(random()::text)
FROM generate_series(1, ${lineitem_rows}) AS s(i)
ON CONFLICT DO NOTHING;
SQL
else
    log_step "lineitem already populated"
fi

log_step "inserting narrow"
if ! table_has_data "narrow"; then
"$PSQL" "${CONN}" -v ON_ERROR_STOP=1 <<SQL
INSERT INTO ${SCHEMA}.narrow (id, v_int, v_text, v_flag)
SELECT i, (i % 1000), md5(i::text), (i % 2 = 0)
FROM generate_series(1, ${narrow_rows}) AS s(i)
ON CONFLICT DO NOTHING;
SQL
else
    log_step "narrow already populated"
fi

log_step "inserting wide"
if ! table_has_data "wide"; then
"$PSQL" "${CONN}" -v ON_ERROR_STOP=1 <<SQL
INSERT INTO ${SCHEMA}.wide (id, v_smallint, v_int, v_bigint, v_numeric, v_real, v_double, v_bool,
                            v_date, v_ts, v_text, v_jsonb, v_bytea, v_uuid, v_char, v_varchar)
SELECT i,
       (i % 32767)::smallint,
       i,
       (i::bigint * 1000),
       (random() * 100000)::numeric(18,4),
       (random() * 1000)::real,
       (random() * 1000)::double precision,
       (i % 2 = 0),
       date '2010-01-01' + (i % 3650),
       timestamp '2010-01-01 00:00:00' + ((i % 3650) || ' days')::interval,
       md5(i::text),
       jsonb_build_object('id', i, 'v', md5(random()::text)),
       decode(md5(i::text), 'hex'),
       gen_random_uuid(),
       lpad((i % 1000000)::text, 8, '0'),
       md5(random()::text)
FROM generate_series(1, ${wide_rows}) AS s(i)
ON CONFLICT DO NOTHING;
SQL
else
    log_step "wide already populated"
fi
