#pragma once
//#ifndef DATABASECONNECTION_H
//#define DATABASECONNECTION_H
//
//#include <string>
//#include <pqxx/pqxx>  // Librería de PostgreSQL para C++
//
//class DatabaseConnection {
//protected:
//    pqxx::connection* conn;  // Conexión a PostgreSQL
//
//    // Parámetros de conexión para PostgreSQL
//    std::string server = "localhost",
//        port = "5432",
//        database = "TheTradingProyectDB",
//        user = "postgres",
//        password = "43481825";  // Cambia por tu contraseña real
//
//    void connectToDatabase();
//    void handle_error(const std::exception& e);
//    void disconnectFromDatabase();
//
//public:
//    long long getLastEventTimestamp();
//    DatabaseConnection();
//    ~DatabaseConnection();
//};
//
//#endif // DATABASECONNECTION_H