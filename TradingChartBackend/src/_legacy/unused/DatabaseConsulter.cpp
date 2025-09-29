//#include "DatabaseConsulter.h"
//#include <iostream>
//#include <codecvt>
//
//
//bool DatabaseConsulter::SelectOpenPrice(const std::string& timestamp, float& openPrice) {
//    SQLHSTMT hStmt;
//    SQLRETURN ret;
//    SQLLEN indicator;
//
//    // Asignar el manejador de declaración
//    SQLAllocHandle(SQL_HANDLE_STMT, hDbc, &hStmt);
//
//    // Construir la consulta SQL
//    std::string query = "SELECT OpenPrice FROM HistoricalData WHERE EventDateTime = ?";
//    std::wstring wQuery(query.begin(), query.end());
//
//    // Preparar la consulta
//    ret = SQLPrepareW(hStmt, (SQLWCHAR*)wQuery.c_str(), SQL_NTS);
//    if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO) {
//        std::wcerr << L"Failed to prepare query to fetch open price." << std::endl;
//        SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
//        return false;
//    }
//
//    // Vincular el parámetro para el timestamp
//    ret = SQLBindParameter(hStmt, 1, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_TYPE_TIMESTAMP, timestamp.length(), 0, (SQLPOINTER)timestamp.c_str(), 0, nullptr);
//    if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO) {
//        std::wcerr << L"Failed to bind parameter for timestamp." << std::endl;
//        SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
//        return false;
//    }
//
//    // Ejecutar la consulta
//    ret = SQLExecute(hStmt);
//    if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO) {
//        std::wcerr << L"Failed to execute query to fetch open price." << std::endl;
//        SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
//        return false;
//    }
//
//    // Leer el resultado
//    if (SQLFetch(hStmt) == SQL_SUCCESS || SQLFetch(hStmt) == SQL_SUCCESS_WITH_INFO) {
//        ret = SQLGetData(hStmt, 1, SQL_C_FLOAT, &openPrice, sizeof(openPrice), &indicator);
//        if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO) {
//            std::wcerr << L"Failed to get OpenPrice." << std::endl;
//            openPrice = 0;
//            SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
//            return false;
//        }
//    }
//    else {
//        std::wcerr << L"Failed to fetch data." << std::endl;
//        openPrice = 0;
//        SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
//        return false;
//    }
//
//    // Limpiar el manejador
//    SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
//    return true;
//}
//
//bool DatabaseConsulter::SelectClosePrice(const std::string& timestamp, float& closePrice) {
//    SQLHSTMT hStmt;
//    SQLRETURN ret;
//    SQLLEN indicator;
//
//    // Asignar el manejador de declaración
//    SQLAllocHandle(SQL_HANDLE_STMT, hDbc, &hStmt);
//
//    // Construir la consulta SQL
//    std::string query = "SELECT ClosePrice FROM HistoricalData WHERE EventDateTime = ?";
//    std::wstring wQuery(query.begin(), query.end());
//
//    // Preparar la consulta
//    ret = SQLPrepareW(hStmt, (SQLWCHAR*)wQuery.c_str(), SQL_NTS);
//    if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO) {
//        std::wcerr << L"Failed to prepare query to fetch close price." << std::endl;
//        SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
//        return false;
//    }
//
//    // Vincular el parámetro para el timestamp
//    ret = SQLBindParameter(hStmt, 1, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_TYPE_TIMESTAMP, timestamp.length(), 0, (SQLPOINTER)timestamp.c_str(), 0, nullptr);
//    if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO) {
//        std::wcerr << L"Failed to bind parameter for timestamp." << std::endl;
//        SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
//        return false;
//    }
//
//    // Ejecutar la consulta
//    ret = SQLExecute(hStmt);
//    if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO) {
//        std::wcerr << L"Failed to execute query to fetch close price." << std::endl;
//        SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
//        return false;
//    }
//
//    // Leer el resultado
//    if (SQLFetch(hStmt) == SQL_SUCCESS || SQLFetch(hStmt) == SQL_SUCCESS_WITH_INFO) {
//        ret = SQLGetData(hStmt, 1, SQL_C_FLOAT, &closePrice, sizeof(closePrice), &indicator);
//        if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO) {
//            std::wcerr << L"Failed to get ClosePrice." << std::endl;
//            closePrice = 0;
//            SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
//            return false;
//        }
//    }
//    else {
//        std::wcerr << L"Failed to fetch data." << std::endl;
//        closePrice = 0;
//        SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
//        return false;
//    }
//
//    // Limpiar el manejador
//    SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
//    return true;
//}
//
//bool DatabaseConsulter::SelectHighPrice(const std::string& timestamp, float& highPrice) {
//    SQLHSTMT hStmt;
//    SQLRETURN ret;
//    SQLLEN indicator;
//
//    // Asignar el manejador de declaración
//    SQLAllocHandle(SQL_HANDLE_STMT, hDbc, &hStmt);
//
//    // Construir la consulta SQL
//    std::string query = "SELECT HighPrice FROM HistoricalData WHERE EventDateTime = ?";
//    std::wstring wQuery(query.begin(), query.end());
//
//    // Preparar la consulta
//    ret = SQLPrepareW(hStmt, (SQLWCHAR*)wQuery.c_str(), SQL_NTS);
//    if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO) {
//        std::wcerr << L"Failed to prepare query to fetch high price." << std::endl;
//        SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
//        return false;
//    }
//
//    // Vincular el parámetro para el timestamp
//    ret = SQLBindParameter(hStmt, 1, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_TYPE_TIMESTAMP, timestamp.length(), 0, (SQLPOINTER)timestamp.c_str(), 0, nullptr);
//    if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO) {
//        std::wcerr << L"Failed to bind parameter for timestamp." << std::endl;
//        SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
//        return false;
//    }
//
//    // Ejecutar la consulta
//    ret = SQLExecute(hStmt);
//    if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO) {
//        std::wcerr << L"Failed to execute query to fetch high price." << std::endl;
//        SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
//        return false;
//    }
//
//    // Leer el resultado
//    if (SQLFetch(hStmt) == SQL_SUCCESS || SQLFetch(hStmt) == SQL_SUCCESS_WITH_INFO) {
//        ret = SQLGetData(hStmt, 1, SQL_C_FLOAT, &highPrice, sizeof(highPrice), &indicator);
//        if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO) {
//            std::wcerr << L"Failed to get HighPrice." << std::endl;
//            highPrice = 0;
//            SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
//            return false;
//        }
//    }
//    else {
//        std::wcerr << L"Failed to fetch data." << std::endl;
//        highPrice = 0;
//        SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
//        return false;
//    }
//
//    // Limpiar el manejador
//    SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
//    return true;
//}
//
//bool DatabaseConsulter::SelectLowPrice(const std::string& timestamp, float& lowPrice) {
//    SQLHSTMT hStmt;
//    SQLRETURN ret;
//    SQLLEN indicator;
//
//    // Asignar el manejador de declaración
//    SQLAllocHandle(SQL_HANDLE_STMT, hDbc, &hStmt);
//
//    // Construir la consulta SQL
//    std::string query = "SELECT LowPrice FROM HistoricalData WHERE EventDateTime = ?";
//    std::wstring wQuery(query.begin(), query.end());
//
//    // Preparar la consulta
//    ret = SQLPrepareW(hStmt, (SQLWCHAR*)wQuery.c_str(), SQL_NTS);
//    if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO) {
//        std::wcerr << L"Failed to prepare query to fetch low price." << std::endl;
//        SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
//        return false;
//    }
//
//    // Vincular el parámetro para el timestamp
//    ret = SQLBindParameter(hStmt, 1, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_TYPE_TIMESTAMP, timestamp.length(), 0, (SQLPOINTER)timestamp.c_str(), 0, nullptr);
//    if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO) {
//        std::wcerr << L"Failed to bind parameter for timestamp." << std::endl;
//        SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
//        return false;
//    }
//
//    // Ejecutar la consulta
//    ret = SQLExecute(hStmt);
//    if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO) {
//        std::wcerr << L"Failed to execute query to fetch low price." << std::endl;
//        SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
//        return false;
//    }
//
//    // Leer el resultado
//    if (SQLFetch(hStmt) == SQL_SUCCESS || SQLFetch(hStmt) == SQL_SUCCESS_WITH_INFO) {
//        ret = SQLGetData(hStmt, 1, SQL_C_FLOAT, &lowPrice, sizeof(lowPrice), &indicator);
//        if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO) {
//            std::wcerr << L"Failed to get LowPrice." << std::endl;
//            lowPrice = 0;
//            SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
//            return false;
//        }
//    }
//    else {
//        std::wcerr << L"Failed to fetch data." << std::endl;
//        lowPrice = 0;
//        SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
//        return false;
//    }
//
//    // Limpiar el manejador
//    SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
//    return true;
//}
//
//long long DatabaseConsulter::getLastEventDataID() {
//    SQLHSTMT hStmt;
//    SQLAllocHandle(SQL_HANDLE_STMT, hDbc, &hStmt);
//
//    // Ejecutar la consulta para obtener el DataID del registro con el EventTimestamp más reciente
//    SQLRETURN ret = SQLExecDirectW(hStmt, (SQLWCHAR*)L"SELECT DataID FROM HistoricalData WHERE EventTimestamp = (SELECT MAX(EventTimestamp) FROM HistoricalData)", SQL_NTS);
//    if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO) {
//        std::wcerr << L"Failed to execute query to fetch DataID for last EventTimestamp." << std::endl;
//        SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
//        return 0; // Retornar 0 en caso de error
//    }
//
//    // Leer el resultado usando SQLGetData
//    long long dataID = 0;
//    SQLLEN indicator; // Variable para almacenar la longitud del dato
//
//    // Fetch para obtener el primer (y único) resultado
//    if (SQLFetch(hStmt) == SQL_SUCCESS || SQLFetch(hStmt) == SQL_SUCCESS_WITH_INFO) {
//        // Leer el dato en la primera columna (índice 1)
//        ret = SQLGetData(hStmt, 1, SQL_C_SBIGINT, &dataID, sizeof(dataID), &indicator);
//        if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO) {
//            std::wcerr << L"Failed to get data." << std::endl;
//            dataID = 0; // Asignar 0 si no se puede obtener el dato
//        }
//        else if (indicator != SQL_NULL_DATA) { // Verificar que haya datos
//            // Asignar el valor de dataID solo si hay datos
//        }
//        else {
//            std::wcerr << L"No data found for DataID." << std::endl;
//            dataID = 0; // Asignar 0 si no hay datos
//        }
//    }
//    else {
//        std::wcerr << L"Failed to fetch data." << std::endl;
//        dataID = 0; // Asignar 0 si no hay datos
//    }
//
//    // Limpiar el manejador
//    SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
//    return dataID;
//}
//
//long long DatabaseConsulter::getSecondLastEventDataID() {
//    SQLHSTMT hStmt;
//    SQLAllocHandle(SQL_HANDLE_STMT, hDbc, &hStmt);
//
//    // Ejecutar la consulta para obtener el DataID del segundo EventTimestamp más reciente
//    SQLRETURN ret = SQLExecDirectW(hStmt, (SQLWCHAR*)L"SELECT DataID FROM HistoricalData ORDER BY EventTimestamp DESC OFFSET 1 ROW FETCH NEXT 1 ROW ONLY", SQL_NTS);
//    if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO) {
//        std::wcerr << L"Failed to execute query to fetch DataID for the second last EventTimestamp." << std::endl;
//        SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
//        return 0; // Retornar 0 en caso de error
//    }
//
//    // Leer el resultado usando SQLGetData
//    long long dataID = 0;
//    SQLLEN indicator; // Variable para almacenar la longitud del dato
//
//    // Fetch para obtener el primer (y único) resultado
//    if (SQLFetch(hStmt) == SQL_SUCCESS || SQLFetch(hStmt) == SQL_SUCCESS_WITH_INFO) {
//        // Leer el dato en la primera columna (índice 1)
//        ret = SQLGetData(hStmt, 1, SQL_C_SBIGINT, &dataID, sizeof(dataID), &indicator);
//        if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO) {
//            std::wcerr << L"Failed to get data." << std::endl;
//            dataID = 0; // Asignar 0 si no se puede obtener el dato
//        }
//        else if (indicator != SQL_NULL_DATA) { // Verificar que haya datos
//            // Asignar el valor de dataID solo si hay datos
//        }
//        else {
//            std::wcerr << L"No data found for DataID." << std::endl;
//            dataID = 0; // Asignar 0 si no hay datos
//        }
//    }
//    else {
//        std::wcerr << L"Failed to fetch data." << std::endl;
//        dataID = 0; // Asignar 0 si no hay datos
//    }
//
//    // Limpiar el manejador
//    SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
//    return dataID;
//}
