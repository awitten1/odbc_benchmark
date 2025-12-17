
#include <cstdlib>
#include <memory>
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

struct Args {
    std::string password;
    uint16_t port;
    std::string host;
    std::string dbname;
    std::string user;
    bool use_async_libpq = false;
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
        } else if (key == "--use_async") {
            args.use_async_libpq = value == "true";
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
    print_pg_result(result);
    PQclear(result);
}

void use_async_libpq(PGconn* conn, const char* query, int chunk_size = 1000) {
    int ret = PQsendQuery(conn, query);
    if (ret == 0) {
        fprintf(stderr, "failed to run sendQuery %s", PQerrorMessage(conn));
        return;
    }

    PQsetChunkedRowsMode(conn, chunk_size);

    for (;;) {
        PGresult* result = PQgetResult(conn);
        if (!result) {
            break;
        }
        print_pg_result(result);
        PQclear(result);
    }

}

int main(int argc, char** argv) {
    Args args = parse_cli_params(argc, argv);
    auto free_pg_conn = [](PGconn* conn) {
        PQfinish(conn);
    };
    std::unique_ptr<PGconn,decltype(free_pg_conn)> conn(
        connect(args),free_pg_conn);

    const char* query = "select * from x;";
    if (!args.use_async_libpq) {
        use_synchronous_libpq(conn.get(), query);
    } else {
        use_async_libpq(conn.get(), query);
    }


    return 0;
}
