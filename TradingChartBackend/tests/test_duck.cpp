// tests/test_duck.cpp
#include <iostream>
#include "duckdb.hpp"

int main() {
    try {
        duckdb::DuckDB db(nullptr);   // DB en memoria
        duckdb::Connection con(db);

        con.Query("CREATE TABLE t(i INTEGER, s VARCHAR)");
        con.Query("INSERT INTO t VALUES (42, 'hola'), (7, 'duck')");

        auto res = con.Query("SELECT i, s FROM t ORDER BY i");

        // En 0.10.x 'success' es protected. Usa GetError() o HasError().
        if (!res || !res->GetError().empty()) {
            std::cerr << "Query error: " << res->GetError() << "\n";
            return 1;
        }

        res->Print();
    } catch (std::exception &ex) {
        std::cerr << "Exception: " << ex.what() << "\n";
        return 1;
    }
    return 0;
}
