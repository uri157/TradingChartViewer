//#include "DatabaseConnection.h"
//#include <iostream>
//
//// Constructor
//DatabaseConnection::DatabaseConnection() : conn(nullptr) {
//    connectToDatabase();
//}
//
//// Destructor
//DatabaseConnection::~DatabaseConnection() {
//    disconnectFromDatabase();
//}
//
//// Conectar a la base de datos PostgreSQL
//void DatabaseConnection::connectToDatabase() {
//    try {
//        std::string connectionString = "host=" + server + " port=" + port + " dbname=" + database +
//            " user=" + user + " password=" + password;
//        conn = new pqxx::connection(connectionString);
//
//        if (conn->is_open()) {
//            std::cout << "Connection to PostgreSQL database successful!" << std::endl;
//        }
//        else {
//            std::cerr << "Connection to PostgreSQL database failed!" << std::endl;
//        }
//    }
//    catch (const std::exception& e) {
//        handle_error(e);
//    }
//}
//
//// Manejo de errores
//void DatabaseConnection::handle_error(const std::exception& e) {
//    std::cerr << "Exception occurred: " << e.what() << std::endl;
//}
//
//// Desconectar de la base de datos PostgreSQL
//void DatabaseConnection::disconnectFromDatabase() {
//    if (conn) {
//        conn->disconnect();
//        delete conn;
//        conn = nullptr;
//    }
//}
//
//// Obtener el último EventTimestamp
//long long DatabaseConnection::getLastEventTimestamp() {
//    try {
//        pqxx::work txn(*conn);
//        pqxx::result r = txn.exec("SELECT MAX(eventtimestamp) FROM historicaldata");
//
//        if (!r.empty() && !r[0][0].is_null()) {
//            return r[0][0].as<long long>();
//        }
//        else {
//            std::cerr << "No data found for EventTimestamp." << std::endl;
//            return 0;
//        }
//    }
//    catch (const std::exception& e) {
//        handle_error(e);
//        return 0;
//    }
//}
