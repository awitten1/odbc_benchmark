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
#include <string>

#include "adbc_utils.h"

struct Args {
    std::string password;
    uint16_t port = 0;
    std::string host;
    std::string dbname;
    std::string user;
    std::string dsn;
    std::string driver;
};

static Args g_args;
static int64_t g_tuples = 1000;

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
        }
    }
    return args;
}

static std::string make_query(int64_t tuples) {
    std::ostringstream os;
    os << "select * from x limit " << tuples << ";";
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
        PQclear(result);
    }
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

static void BenchLibpqSync(benchmark::State& state) {
    const int64_t tuples = state.range(0);
    const std::string query = make_query(tuples);

    PGconn* conn = connect_libpq(g_args);
    if (!conn) {
        state.SkipWithError("libpq connect failed");
        return;
    }

    for (auto _ : state) {
        use_synchronous_libpq(conn, query.c_str());
    }

    PQfinish(conn);
    state.SetItemsProcessed(state.iterations() * tuples);
}

static void BenchLibpqAsync(benchmark::State& state) {
    const int64_t tuples = state.range(0);
    const std::string query = make_query(tuples);

    PGconn* conn = connect_libpq(g_args);
    if (!conn) {
        state.SkipWithError("libpq connect failed");
        return;
    }

    for (auto _ : state) {
        use_async_libpq(conn, query.c_str());
    }

    PQfinish(conn);
    state.SetItemsProcessed(state.iterations() * tuples);
}

static void BenchOdbc(benchmark::State& state) {
    const int64_t tuples = state.range(0);
    const std::string query = make_query(tuples);

    OdbcHandles handles;
    if (!odbc_connect(g_args, &handles)) {
        odbc_disconnect(&handles);
        state.SkipWithError("ODBC connect failed");
        return;
    }

    for (auto _ : state) {
        SQLRETURN rc = SQLExecDirect(handles.stmt, (SQLCHAR*)query.c_str(), SQL_NTS);
        if (rc == SQL_SUCCESS || rc == SQL_SUCCESS_WITH_INFO) {
            while (SQLFetch(handles.stmt) == SQL_SUCCESS) {
            }
        }
        SQLCloseCursor(handles.stmt);
    }

    odbc_disconnect(&handles);
    state.SetItemsProcessed(state.iterations() * tuples);
}

static void BenchAdbc(benchmark::State& state) {
    const int64_t tuples = state.range(0);
    const std::string query = make_query(tuples);

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

    AdbcConnectionRelease(&connection, &error);
    AdbcDatabaseRelease(&database, &error);
    state.SetItemsProcessed(state.iterations() * tuples);
}

int main(int argc, char** argv) {
    g_args = parse_cli_params_lenient(argc, argv);

    benchmark::RegisterBenchmark("libpq_sync", BenchLibpqSync)->Arg(g_tuples);
    benchmark::RegisterBenchmark("libpq_async", BenchLibpqAsync)->Arg(g_tuples);
    benchmark::RegisterBenchmark("odbc", BenchOdbc)->Arg(g_tuples);
    benchmark::RegisterBenchmark("adbc", BenchAdbc)->Arg(g_tuples);

    benchmark::Initialize(&argc, argv);
    benchmark::RunSpecifiedBenchmarks();
    benchmark::Shutdown();
    return 0;
}
