#include <benchmark/benchmark.h>

#include <cstdlib>
#include <cstring>
#include <memory>
#include <postgresql/libpq-fe.h>
#include <sqltypes.h>
#include <sstream>
#include <stdexcept>
#include <stdio.h>
#include <stdlib.h>
#include <sql.h>
#include <sqlext.h>
#include <libpq-fe.h>
#include <sys/resource.h>
#include <string>
#include <unistd.h>

#include "adbc_utils.h"

struct Args {
    std::string password;
    uint16_t port = 5432;
    std::string host = "localhost";
    std::string dbname = "postgres";
    std::string user = getlogin();
    std::string dsn;
    std::string driver;
};

static Args g_args;
static int64_t g_tuples = 1000;
static std::string g_tables_filter;

static Args parse_cli_params_lenient(int argc, char** argv) {
    Args args;
    for (int i = 1; i < argc; ++i) {
        std::string arg(argv[i]);
        auto pos = arg.find("=");
        if (pos == std::string::npos) {
            continue;
        }
        std::string key(arg.begin(), arg.begin() + pos);
        std::string value(arg.begin() + pos + 1, arg.end());
        if (key == "--password") {
            args.password = value;
        } else if (key == "--host") {
            args.host = value;
        } else if (key == "--port") {
            args.port = std::stoi(value);
        } else if (key == "--dbname") {
            args.dbname = value;
        } else if (key == "--user") {
            args.user = value;
        } else if (key == "--dsn") {
            args.dsn = value;
        } else if (key == "--driver") {
            args.driver = value;
        } else if (key == "--tuples") {
            g_tuples = std::stoll(value);
        } else if (key == "--tables") {
            g_tables_filter = value;
        }
    }
    return args;
}

struct TableSpec {
    const char* name;
    const char* pk;
};

static const TableSpec kTables[] = {
    {"narrow", "id"},
    {"wide", "id"},
};

static std::string make_query(const TableSpec& table, int64_t tuples) {
    std::ostringstream os;
    os << "select * from bench." << table.name << " order by " << table.pk
       << " limit " << tuples << ";";
    return os.str();
}

static std::string make_copy_query(const TableSpec& table, int64_t tuples) {
    std::ostringstream os;
    os << "copy (select * from bench." << table.name << " order by " << table.pk
       << " limit " << tuples << ") to stdout with (format binary);";
    return os.str();
}

static PGconn* connect_libpq(const Args& args) {
    std::ostringstream os;
    os << "user=" << args.user << " " << "dbname=" << args.dbname << " port=" << args.port
       << " host=" << args.host;
    std::string s = os.str();
    PGconn* c = PQconnectdb(s.c_str());
    if (PQstatus(c) != CONNECTION_OK) {
        PQfinish(c);
        return nullptr;
    }
    return c;
}

static void use_synchronous_libpq(PGconn* conn, const char* query) {
    PGresult* result = PQexec(conn, query);
    if (!result) {
        return;
    }
    ExecStatusType status = PQresultStatus(result);
    if (status == PGRES_TUPLES_OK || status == PGRES_SINGLE_TUPLE ||
        status == PGRES_TUPLES_CHUNK) {
        int nrows = PQntuples(result);
        int ncols = PQnfields(result);
        size_t total_bytes = 0;
        for (int row = 0; row < nrows; ++row) {
            for (int col = 0; col < ncols; ++col) {
                if (!PQgetisnull(result, row, col)) {
                    total_bytes += static_cast<size_t>(PQgetlength(result, row, col));
                    (void)PQgetvalue(result, row, col);
                }
            }
        }
        benchmark::DoNotOptimize(total_bytes);
    }
    PQclear(result);
}

static void use_async_libpq(PGconn* conn, const char* query, int chunk_size = 1000) {
    int ret = PQsendQuery(conn, query);
    if (ret == 0) {
        return;
    }

    PQsetChunkedRowsMode(conn, chunk_size);

    for (;;) {
        PGresult* result = PQgetResult(conn);
        if (!result) {
            break;
        }
        ExecStatusType status = PQresultStatus(result);
        if (status == PGRES_TUPLES_OK || status == PGRES_SINGLE_TUPLE ||
            status == PGRES_TUPLES_CHUNK) {
            int nrows = PQntuples(result);
            int ncols = PQnfields(result);
            size_t total_bytes = 0;
            for (int row = 0; row < nrows; ++row) {
                for (int col = 0; col < ncols; ++col) {
                    if (!PQgetisnull(result, row, col)) {
                        total_bytes += static_cast<size_t>(PQgetlength(result, row, col));
                        (void)PQgetvalue(result, row, col);
                    }
                }
            }
            benchmark::DoNotOptimize(total_bytes);
        }
        PQclear(result);
    }
}

static bool use_copy_to_stdout_libpq(PGconn* conn, const char* query) {
    PGresult* result = PQexec(conn, query);
    if (!result) {
        return false;
    }
    if (PQresultStatus(result) != PGRES_COPY_OUT) {
        PQclear(result);
        return false;
    }
    PQclear(result);

    size_t total_bytes = 0;
    for (;;) {
        char* buf = nullptr;
        int len = PQgetCopyData(conn, &buf, 0);
        if (len > 0) {
            total_bytes += static_cast<size_t>(len);
            PQfreemem(buf);
            continue;
        }
        if (len == -1) {
            break;
        }
        if (len == -2) {
            return false;
        }
    }
    benchmark::DoNotOptimize(total_bytes);

    for (;;) {
        PGresult* r = PQgetResult(conn);
        if (!r) {
            break;
        }
        PQclear(r);
    }
    return true;
}

static bool use_copy_to_stdout_libpq_async(PGconn* conn, const char* query) {
    if (PQsendQuery(conn, query) == 0) {
        return false;
    }

    PGresult* result = nullptr;
    for (;;) {
        result = PQgetResult(conn);
        if (!result) {
            return false;
        }
        ExecStatusType status = PQresultStatus(result);
        if (status == PGRES_COPY_OUT) {
            PQclear(result);
            break;
        }
        if (status != PGRES_TUPLES_OK && status != PGRES_COMMAND_OK) {
            PQclear(result);
            return false;
        }
        PQclear(result);
    }

    size_t total_bytes = 0;
    for (;;) {
        char* buf = nullptr;
        int len = PQgetCopyData(conn, &buf, 0);
        if (len > 0) {
            total_bytes += static_cast<size_t>(len);
            PQfreemem(buf);
            continue;
        }
        if (len == -1) {
            break;
        }
        if (len == -2) {
            return false;
        }
    }
    benchmark::DoNotOptimize(total_bytes);

    for (;;) {
        PGresult* r = PQgetResult(conn);
        if (!r) {
            break;
        }
        PQclear(r);
    }
    return true;
}

struct OdbcHandles {
    SQLHENV env = SQL_NULL_HENV;
    SQLHDBC dbc = SQL_NULL_HDBC;
    SQLHSTMT stmt = SQL_NULL_HSTMT;
};

static bool odbc_connect(const Args& args, OdbcHandles* handles) {
    if (SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &handles->env) != SQL_SUCCESS) {
        return false;
    }
    if (SQLSetEnvAttr(handles->env, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)SQL_OV_ODBC3, 0) !=
        SQL_SUCCESS) {
        SQLFreeHandle(SQL_HANDLE_ENV, handles->env);
        handles->env = SQL_NULL_HENV;
        return false;
    }
    if (SQLAllocHandle(SQL_HANDLE_DBC, handles->env, &handles->dbc) != SQL_SUCCESS) {
        SQLFreeHandle(SQL_HANDLE_ENV, handles->env);
        handles->env = SQL_NULL_HENV;
        return false;
    }

    std::ostringstream cs;
    if (!args.dsn.empty()) {
        cs << "DSN=" << args.dsn << ";";
    } else {
        const std::string driver = args.driver.empty() ? "PostgreSQL Unicode" : args.driver;
        cs << "Driver={" << driver << "};";
    }
    if (!args.host.empty()) {
        cs << "Server=" << args.host << ";";
    }
    if (args.port != 0) {
        cs << "Port=" << args.port << ";";
    }
    if (!args.dbname.empty()) {
        cs << "Database=" << args.dbname << ";";
    }
    if (!args.user.empty()) {
        cs << "Uid=" << args.user << ";";
    }
    if (!args.password.empty()) {
        cs << "Pwd=" << args.password << ";";
    }
    const std::string conn_str = cs.str();

    SQLCHAR out_conn[1024];
    SQLSMALLINT out_len = 0;
    SQLRETURN rc = SQLDriverConnect(
        handles->dbc,
        nullptr,
        (SQLCHAR*)conn_str.c_str(),
        SQL_NTS,
        out_conn,
        sizeof(out_conn),
        &out_len,
        SQL_DRIVER_NOPROMPT);
    if (!(rc == SQL_SUCCESS || rc == SQL_SUCCESS_WITH_INFO)) {
        return false;
    }

    if (SQLAllocHandle(SQL_HANDLE_STMT, handles->dbc, &handles->stmt) != SQL_SUCCESS) {
        return false;
    }
    return true;
}

static void odbc_disconnect(OdbcHandles* handles) {
    if (handles->stmt != SQL_NULL_HSTMT) {
        SQLFreeHandle(SQL_HANDLE_STMT, handles->stmt);
        handles->stmt = SQL_NULL_HSTMT;
    }
    if (handles->dbc != SQL_NULL_HDBC) {
        SQLDisconnect(handles->dbc);
        SQLFreeHandle(SQL_HANDLE_DBC, handles->dbc);
        handles->dbc = SQL_NULL_HDBC;
    }
    if (handles->env != SQL_NULL_HENV) {
        SQLFreeHandle(SQL_HANDLE_ENV, handles->env);
        handles->env = SQL_NULL_HENV;
    }
}

static bool odbc_is_truncation(SQLHSTMT stmt) {
    SQLCHAR state[6] = {};
    SQLINTEGER native_error = 0;
    SQLCHAR message[256] = {};
    SQLSMALLINT message_len = 0;
    SQLRETURN rc = SQLGetDiagRec(SQL_HANDLE_STMT, stmt, 1, state, &native_error,
                                 message, sizeof(message), &message_len);
    if (rc != SQL_SUCCESS && rc != SQL_SUCCESS_WITH_INFO) {
        return false;
    }
    return std::string(reinterpret_cast<char*>(state)) == "01004";
}

static void check_adbc(AdbcStatusCode status, AdbcError* error) {
    if (status == ADBC_STATUS_OK) {
        return;
    }
    std::string msg = "ADBC error";
    if (error && error->message) {
        msg = error->message;
    }
    if (error && error->release) {
        error->release(error);
    }
    throw std::runtime_error(msg);
}

static void check_stream(struct ArrowArrayStream* stream, int status) {
    if (status == 0) {
        return;
    }
    std::string msg = "Arrow stream error";
    if (stream && stream->get_last_error) {
        const char* last = stream->get_last_error(stream);
        if (last) {
            msg = last;
        }
    }
    throw std::runtime_error(msg);
}

struct CpuUsage {
    double user_s = 0.0;
    double sys_s = 0.0;
};

static CpuUsage read_cpu_usage() {
    struct rusage ru;
    if (getrusage(RUSAGE_SELF, &ru) != 0) {
        return {};
    }
    auto to_seconds = [](const timeval& tv) -> double {
        return static_cast<double>(tv.tv_sec) + static_cast<double>(tv.tv_usec) / 1e6;
    };
    CpuUsage out;
    out.user_s = to_seconds(ru.ru_utime);
    out.sys_s = to_seconds(ru.ru_stime);
    return out;
}

static CpuUsage diff_cpu_usage(const CpuUsage& end, const CpuUsage& start) {
    return {end.user_s - start.user_s, end.sys_s - start.sys_s};
}

static void BenchLibpqSync(benchmark::State& state, const TableSpec& table) {
    const int64_t tuples = state.range(0);
    const std::string query = make_query(table, tuples);

    PGconn* conn = connect_libpq(g_args);
    if (!conn) {
        state.SkipWithError("libpq connect failed");
        return;
    }

    const CpuUsage cpu_start = read_cpu_usage();

    for (auto _ : state) {
        use_synchronous_libpq(conn, query.c_str());
    }

    const CpuUsage cpu_delta = diff_cpu_usage(read_cpu_usage(), cpu_start);

    PQfinish(conn);
    state.SetItemsProcessed(state.iterations() * tuples);
    if (state.iterations() > 0) {
        state.counters["cpu_user_ms"] = (cpu_delta.user_s * 1000.0) / state.iterations();
        state.counters["cpu_sys_ms"] = (cpu_delta.sys_s * 1000.0) / state.iterations();
    }
}

static void BenchLibpqAsync(benchmark::State& state, const TableSpec& table) {
    const int64_t tuples = state.range(0);
    const std::string query = make_query(table, tuples);

    PGconn* conn = connect_libpq(g_args);
    if (!conn) {
        state.SkipWithError("libpq connect failed");
        return;
    }

    const CpuUsage cpu_start = read_cpu_usage();

    for (auto _ : state) {
        use_async_libpq(conn, query.c_str());
    }

    const CpuUsage cpu_delta = diff_cpu_usage(read_cpu_usage(), cpu_start);

    PQfinish(conn);
    state.SetItemsProcessed(state.iterations() * tuples);
    if (state.iterations() > 0) {
        state.counters["cpu_user_ms"] = (cpu_delta.user_s * 1000.0) / state.iterations();
        state.counters["cpu_sys_ms"] = (cpu_delta.sys_s * 1000.0) / state.iterations();
    }
}

static void BenchLibpqCopyToStdout(benchmark::State& state, const TableSpec& table) {
    const int64_t tuples = state.range(0);
    const std::string query = make_copy_query(table, tuples);

    PGconn* conn = connect_libpq(g_args);
    if (!conn) {
        state.SkipWithError("libpq connect failed");
        return;
    }

    const CpuUsage cpu_start = read_cpu_usage();

    for (auto _ : state) {
        if (!use_copy_to_stdout_libpq(conn, query.c_str())) {
            state.SkipWithError("libpq copy to stdout failed");
            break;
        }
    }

    const CpuUsage cpu_delta = diff_cpu_usage(read_cpu_usage(), cpu_start);

    PQfinish(conn);
    state.SetItemsProcessed(state.iterations() * tuples);
    if (state.iterations() > 0) {
        state.counters["cpu_user_ms"] = (cpu_delta.user_s * 1000.0) / state.iterations();
        state.counters["cpu_sys_ms"] = (cpu_delta.sys_s * 1000.0) / state.iterations();
    }
}

static void BenchLibpqAsyncCopyToStdout(benchmark::State& state, const TableSpec& table) {
    const int64_t tuples = state.range(0);
    const std::string query = make_copy_query(table, tuples);

    PGconn* conn = connect_libpq(g_args);
    if (!conn) {
        state.SkipWithError("libpq connect failed");
        return;
    }

    const CpuUsage cpu_start = read_cpu_usage();

    for (auto _ : state) {
        if (!use_copy_to_stdout_libpq_async(conn, query.c_str())) {
            state.SkipWithError("libpq async copy to stdout failed");
            break;
        }
    }

    const CpuUsage cpu_delta = diff_cpu_usage(read_cpu_usage(), cpu_start);

    PQfinish(conn);
    state.SetItemsProcessed(state.iterations() * tuples);
    if (state.iterations() > 0) {
        state.counters["cpu_user_ms"] = (cpu_delta.user_s * 1000.0) / state.iterations();
        state.counters["cpu_sys_ms"] = (cpu_delta.sys_s * 1000.0) / state.iterations();
    }
}

static void BenchOdbc(benchmark::State& state, const TableSpec& table) {
    const int64_t tuples = state.range(0);
    const std::string query = make_query(table, tuples);

    OdbcHandles handles;
    if (!odbc_connect(g_args, &handles)) {
        odbc_disconnect(&handles);
        state.SkipWithError("ODBC connect failed");
        return;
    }

    const CpuUsage cpu_start = read_cpu_usage();

    for (auto _ : state) {
        size_t total_bytes = 0;
        SQLRETURN rc = SQLExecDirect(handles.stmt, (SQLCHAR*)query.c_str(), SQL_NTS);
        if (rc == SQL_SUCCESS || rc == SQL_SUCCESS_WITH_INFO) {
            SQLSMALLINT cols = 0;
            if (SQLNumResultCols(handles.stmt, &cols) == SQL_SUCCESS && cols > 0) {
                std::string buffer(4096, '\0');
                    while (SQLFetch(handles.stmt) == SQL_SUCCESS) {
                        for (SQLUSMALLINT col = 1; col <= cols; ++col) {
                            for (;;) {
                                SQLLEN out_len = 0;
                                rc = SQLGetData(handles.stmt, col, SQL_C_CHAR,
                                                buffer.data(), buffer.size(), &out_len);
                                if (rc == SQL_ERROR || rc == SQL_INVALID_HANDLE) {
                                    state.SkipWithError("ODBC fetch failed");
                                    break;
                                }
                                if (rc == SQL_NO_DATA) {
                                    break;
                                }
                                if (out_len == SQL_NULL_DATA) {
                                    break;
                                }
                            if (out_len == SQL_NO_TOTAL) {
                                total_bytes += buffer.size();
                            } else {
                                total_bytes += static_cast<size_t>(
                                    out_len < 0 ? 0 : out_len);
                            }
                            if (rc == SQL_SUCCESS) {
                                break;
                            }
                            if (rc == SQL_SUCCESS_WITH_INFO &&
                                !odbc_is_truncation(handles.stmt)) {
                                break;
                            }
                        }
                        if (state.skipped()) {
                            break;
                        }
                    }
                    if (state.skipped()) {
                        break;
                    }
                }
            } else {
                state.SkipWithError("ODBC result metadata failed");
            }
        } else {
            state.SkipWithError("ODBC exec failed");
        }
        SQLCloseCursor(handles.stmt);
        benchmark::DoNotOptimize(total_bytes);
        if (state.skipped()) {
            break;
        }
    }

    const CpuUsage cpu_delta = diff_cpu_usage(read_cpu_usage(), cpu_start);

    odbc_disconnect(&handles);
    state.SetItemsProcessed(state.iterations() * tuples);
    if (state.iterations() > 0) {
        state.counters["cpu_user_ms"] = (cpu_delta.user_s * 1000.0) / state.iterations();
        state.counters["cpu_sys_ms"] = (cpu_delta.sys_s * 1000.0) / state.iterations();
    }
}

static void BenchAdbc(benchmark::State& state, const TableSpec& table) {
    const int64_t tuples = state.range(0);
    const std::string query = make_query(table, tuples);

    struct AdbcError error = {};
    struct AdbcDatabase database = {};
    struct AdbcConnection connection = {};

    std::ostringstream os;
    os << "postgresql://";
    if (!g_args.user.empty()) {
        os << g_args.user;
        if (!g_args.password.empty()) {
            os << ":" << g_args.password;
        }
        os << "@";
    }
    os << g_args.host;
    if (g_args.port != 0) {
        os << ":" << g_args.port;
    }
    if (!g_args.dbname.empty()) {
        os << "/" << g_args.dbname;
    }
    std::string uri = os.str();

    try {
        check_adbc(AdbcDatabaseNew(&database, &error), &error);
        check_adbc(AdbcDatabaseSetOption(&database, "uri", uri.c_str(), &error), &error);
        check_adbc(AdbcDatabaseInit(&database, &error), &error);
        check_adbc(AdbcConnectionNew(&connection, &error), &error);
        check_adbc(AdbcConnectionInit(&connection, &database, &error), &error);
    } catch (const std::exception& ex) {
        state.SkipWithError(ex.what());
        return;
    }

    const CpuUsage cpu_start = read_cpu_usage();

    for (auto _ : state) {
        struct AdbcStatement statement = {};
        struct ArrowArrayStream stream = {};
        int64_t rows_affected = -1;
        try {
            check_adbc(AdbcStatementNew(&connection, &statement, &error), &error);
            check_adbc(AdbcStatementSetSqlQuery(&statement, query.c_str(), &error), &error);
            check_adbc(AdbcStatementExecuteQuery(&statement, &stream, &rows_affected, &error),
                       &error);

            struct ArrowSchema schema = {};
            check_stream(&stream, stream.get_schema(&stream, &schema));
            if (schema.release) {
                schema.release(&schema);
            }
            for (;;) {
                struct ArrowArray array = {};
                check_stream(&stream, stream.get_next(&stream, &array));
                if (array.release == nullptr) {
                    break;
                }
                array.release(&array);
            }
            if (stream.release) {
                stream.release(&stream);
            }
            check_adbc(AdbcStatementRelease(&statement, &error), &error);
        } catch (const std::exception& ex) {
            if (stream.release) {
                stream.release(&stream);
            }
            AdbcStatementRelease(&statement, &error);
            state.SkipWithError(ex.what());
            break;
        }
    }

    const CpuUsage cpu_delta = diff_cpu_usage(read_cpu_usage(), cpu_start);

    AdbcConnectionRelease(&connection, &error);
    AdbcDatabaseRelease(&database, &error);
    state.SetItemsProcessed(state.iterations() * tuples);
    if (state.iterations() > 0) {
        state.counters["cpu_user_ms"] = (cpu_delta.user_s * 1000.0) / state.iterations();
        state.counters["cpu_sys_ms"] = (cpu_delta.sys_s * 1000.0) / state.iterations();
    }
}

int main(int argc, char** argv) {
    g_args = parse_cli_params_lenient(argc, argv);

    for (const auto& table : kTables) {
        if (!g_tables_filter.empty() &&
            g_tables_filter.find(table.name) == std::string::npos) {
            continue;
        }
        const std::string base = table.name;
        benchmark::RegisterBenchmark(("libpq_sync/" + base).c_str(),
                                     [&table](benchmark::State& state) {
                                         BenchLibpqSync(state, table);
                                     })->Arg(g_tuples);
        benchmark::RegisterBenchmark(("libpq_async/" + base).c_str(),
                                     [&table](benchmark::State& state) {
                                         BenchLibpqAsync(state, table);
                                     })->Arg(g_tuples);
        benchmark::RegisterBenchmark(("libpq_copy_stdout/" + base).c_str(),
                                     [&table](benchmark::State& state) {
                                         BenchLibpqCopyToStdout(state, table);
                                     })->Arg(g_tuples);
        benchmark::RegisterBenchmark(("libpq_async_copy_stdout/" + base).c_str(),
                                     [&table](benchmark::State& state) {
                                         BenchLibpqAsyncCopyToStdout(state, table);
                                     })->Arg(g_tuples);
        benchmark::RegisterBenchmark(("odbc/" + base).c_str(),
                                     [&table](benchmark::State& state) {
                                         BenchOdbc(state, table);
                                     })->Arg(g_tuples);
        benchmark::RegisterBenchmark(("adbc/" + base).c_str(),
                                     [&table](benchmark::State& state) {
                                         BenchAdbc(state, table);
                                     })->Arg(g_tuples);
    }

    benchmark::Initialize(&argc, argv);
    benchmark::RunSpecifiedBenchmarks();
    benchmark::Shutdown();
    return 0;
}
