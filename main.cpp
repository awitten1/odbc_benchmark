
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
        if (key == "password") {
            args.password = value;
        } else if (key == "host") {
            args.host = value;
        } else if (key == "port") {
            args.port = std::stoi(value);
        } else if (key == "dbname") {
            args.dbname = value;
        } else if (key == "user") {
            args.user = value;
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

int main(int argc, char** argv) {
    Args args = parse_cli_params(argc, argv);
    auto free_pg_conn = [](PGconn* conn) {
        PQfinish(conn);
    };
    std::unique_ptr<PGconn,decltype(free_pg_conn)> conn(
        connect(args),free_pg_conn);

    const char* query = "select * from x;";
    PGresult* result = PQexec(conn.get(), query);
    std::cout << "query " << query << " returned " << PQntuples(result) << " tuples" << std::endl;
    int nrows = PQntuples(result);
    int ncols = PQnfields(result);

    for (int i = 0; i < nrows; i++) {
        for (int j = 0; j < ncols; j++) {
            char *value = PQgetvalue(result, i, j);
            printf(j == ncols - 1 ? "%s" : "%s,", value);
        }
        printf("\n");
    }

    PQclear(result);
    return 0;
}
