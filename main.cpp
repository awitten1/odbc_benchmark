
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
#include <iostream>
#include "adbc_utils.h"

// Ignoring error handling
struct Args {
    std::string password;
    uint16_t port = 0;
    std::string host;
    std::string dbname;
    std::string user;
    std::string dsn;
    std::string driver;
    bool use_async_libpq = false;
    std::string mode = "libpq";
};

Args parse_cli_params(int argc, char** argv) {
    Args args;
    for (int i = 1; i < argc; ++i) {
        std::string arg(argv[i]);
        auto pos = arg.find("=");
        if (pos == std::string::npos) {
            std::cerr << "arguments must be key-value pairs formatted as <key>=<value>"
                << std::endl;
            exit(EXIT_FAILURE);
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
        } else if (key == "--use_async") {
            args.use_async_libpq = value == "true";
        } else if (key == "--mode") {
            args.mode = value;
        } else {
            std::cerr << "unknown key " << key << std::endl;
            exit(EXIT_FAILURE);
        }
    }
    return args;
}


PGconn* connect(Args args) {
    std::ostringstream os;
    os << "user=" << args.user << " " << "dbname=" << args.dbname << " port=" << args.port
         << " host=" << args.host;
    std::string s = os.str();
    std::cout << "connecting to " << s << std::endl;
    PGconn* c = PQconnectdb(s.c_str());
    if (PQstatus(c) != CONNECTION_OK) {
        std::cerr << PQerrorMessage(c) << std::endl;
        fprintf(stderr, "failed to connect to postgres\n");
        exit(EXIT_FAILURE);
    }
    return c;
}


void print_pg_result(PGresult* result) {
    int nrows = PQntuples(result);
    int ncols = PQnfields(result);

    for (int i = 0; i < nrows; i++) {
        for (int j = 0; j < ncols; j++) {
            char *value = PQgetvalue(result, i, j);
            printf(j == ncols - 1 ? "%s" : "%s,", value);
        }
        printf("\n");
    }
}

void use_synchronous_libpq(PGconn* conn, const char* query) {
    PGresult* result = PQexec(conn, query);
    if (!result) {
        fprintf(stderr, "libpq sync exec failed: %s", PQerrorMessage(conn));
        return;
    }
    ExecStatusType status = PQresultStatus(result);
    if (status != PGRES_TUPLES_OK && status != PGRES_COMMAND_OK) {
        const char* msg = PQresultErrorMessage(result);
        if (!msg || msg[0] == '\0') {
            msg = PQerrorMessage(conn);
        }
        fprintf(stderr, "libpq sync exec bad status (%s): %s", PQresStatus(status), msg);
    }
    //print_pg_result(result);
    PQclear(result);
}

void use_async_libpq(PGconn* conn, const char* query, int chunk_size = 1000) {
    int ret = PQsendQuery(conn, query);
    if (ret == 0) {
        fprintf(stderr, "failed to run sendQuery %s", PQerrorMessage(conn));
        return;
    }

    PQsetChunkedRowsMode(conn, chunk_size);

    bool saw_result = false;
    for (;;) {
        PGresult* result = PQgetResult(conn);
        if (!result) {
            break;
        }
        saw_result = true;
        ExecStatusType status = PQresultStatus(result);
        if (status != PGRES_TUPLES_OK && status != PGRES_COMMAND_OK &&
            status != PGRES_SINGLE_TUPLE && status != PGRES_TUPLES_CHUNK) {
            const char* msg = PQresultErrorMessage(result);
            if (!msg || msg[0] == '\0') {
                msg = PQerrorMessage(conn);
            }
            fprintf(stderr, "libpq async exec bad status (%s): %s", PQresStatus(status), msg);
        }
        //print_pg_result(result);
        PQclear(result);
    }
    if (!saw_result) {
        fprintf(stderr, "libpq async exec returned no results: %s", PQerrorMessage(conn));
    }

}

void use_odbc(const Args& args, const char* query) {
    SQLHENV env = SQL_NULL_HENV;
    SQLHDBC dbc = SQL_NULL_HDBC;
    SQLHSTMT stmt = SQL_NULL_HSTMT;

    if (SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &env) != SQL_SUCCESS) {
        return;
    }
    if (SQLSetEnvAttr(env, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)SQL_OV_ODBC3, 0) !=
        SQL_SUCCESS) {
        SQLFreeHandle(SQL_HANDLE_ENV, env);
        return;
    }
    if (SQLAllocHandle(SQL_HANDLE_DBC, env, &dbc) != SQL_SUCCESS) {
        SQLFreeHandle(SQL_HANDLE_ENV, env);
        return;
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
        dbc,
        nullptr,
        (SQLCHAR*)conn_str.c_str(),
        SQL_NTS,
        out_conn,
        sizeof(out_conn),
        &out_len,
        SQL_DRIVER_NOPROMPT);
    if (!(rc == SQL_SUCCESS || rc == SQL_SUCCESS_WITH_INFO)) {
        SQLFreeHandle(SQL_HANDLE_DBC, dbc);
        SQLFreeHandle(SQL_HANDLE_ENV, env);
        return;
    }

    if (SQLAllocHandle(SQL_HANDLE_STMT, dbc, &stmt) != SQL_SUCCESS) {
        SQLDisconnect(dbc);
        SQLFreeHandle(SQL_HANDLE_DBC, dbc);
        SQLFreeHandle(SQL_HANDLE_ENV, env);
        return;
    }

    rc = SQLExecDirect(stmt, (SQLCHAR*)query, SQL_NTS);
    if (rc == SQL_SUCCESS || rc == SQL_SUCCESS_WITH_INFO) {
        while (SQLFetch(stmt) == SQL_SUCCESS) {
            // Intentionally ignore result data for benchmarking.
        }
    }

    SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    SQLDisconnect(dbc);
    SQLFreeHandle(SQL_HANDLE_DBC, dbc);
    SQLFreeHandle(SQL_HANDLE_ENV, env);
}

int use_adbc(const Args& args, const char* query) {
    struct AdbcError error = {};
    struct AdbcDatabase database = {};
    struct AdbcConnection connection = {};
    struct AdbcStatement statement = {};

    std::ostringstream os;
    os << "postgresql://";
    if (!args.user.empty()) {
        os << args.user;
        if (!args.password.empty()) {
            os << ":" << args.password;
        }
        os << "@";
    }
    os << args.host;
    if (args.port != 0) {
        os << ":" << args.port;
    }
    if (!args.dbname.empty()) {
        os << "/" << args.dbname;
    }
    std::string uri = os.str();

    CHECK_ADBC(AdbcDatabaseNew(&database, &error));
    CHECK_ADBC(AdbcDatabaseSetOption(&database, "uri", uri.c_str(), &error));
    CHECK_ADBC(AdbcDatabaseInit(&database, &error));
    CHECK_ADBC(AdbcConnectionNew(&connection, &error));
    CHECK_ADBC(AdbcConnectionInit(&connection, &database, &error));
    CHECK_ADBC(AdbcStatementNew(&connection, &statement, &error));
    CHECK_ADBC(AdbcStatementSetSqlQuery(&statement, query, &error));

    struct ArrowArrayStream stream = {};
    int64_t rows_affected = -1;
    CHECK_ADBC(AdbcStatementExecuteQuery(&statement, &stream, &rows_affected, &error));

    struct ArrowSchema schema = {};
    CHECK_STREAM(stream, stream.get_schema(&stream, &schema));
    if (schema.release != nullptr) {
        schema.release(&schema);
    }
    for (;;) {
        struct ArrowArray array = {};
        CHECK_STREAM(stream, stream.get_next(&stream, &array));
        if (array.release == nullptr) {
            break;
        }
        array.release(&array);
    }
    if (stream.release != nullptr) {
        stream.release(&stream);
    }

    CHECK_ADBC(AdbcStatementRelease(&statement, &error));
    CHECK_ADBC(AdbcConnectionRelease(&connection, &error));
    CHECK_ADBC(AdbcDatabaseRelease(&database, &error));
    return EXIT_SUCCESS;
}

int main(int argc, char** argv) {
    Args args = parse_cli_params(argc, argv);


    const char* query = "select * from x;";
    if (args.mode == "libpq") {
        auto free_pg_conn = [](PGconn* conn) {
            PQfinish(conn);
        };
        std::unique_ptr<PGconn,decltype(free_pg_conn)> conn(
            connect(args),free_pg_conn);
        if (!args.use_async_libpq) {
            use_synchronous_libpq(conn.get(), query);
        } else {
            use_async_libpq(conn.get(), query);
        }
    } else if (args.mode == "adbc") {
        int status = use_adbc(args, query);
        if (status != EXIT_SUCCESS) {
            return status;
        }
    } else if (args.mode == "odbc") {
        use_odbc(args, query);
    }


    return 0;
}
