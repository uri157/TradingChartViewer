//#include "MarketDataActualizer.h"
//#include <vector>
//#include <iostream>
//
//
//
//void MarketDataActualizer::fetchAndInsertData(const std::string& symbol, const std::string& interval) {
//    long long startTime = 0; // Puedes usar 0 para indicar que quieres todos los datos disponibles
//    auto data = cryptoFetcher.fetchHistoricalData(symbol, interval, startTime);
//    insertDataIntoDatabase(data);
//}
//
//
//void MarketDataActualizer::insertDataIntoDatabase(const std::vector<std::vector<std::string>>& data) {
//    try {
//        pqxx::connection conn("dbname=your_db_name user=your_user password=your_password host=your_host");
//        pqxx::work txn(conn);
//
//        // Crear tabla temporal en PostgreSQL
//        txn.exec("CREATE TEMP TABLE TempData ("
//            "BaseAssetID INT, "
//            "CounterAssetID INT, "
//            "IntervalID INT, "
//            "EventTimestamp BIGINT, "
//            "OpenPrice NUMERIC(18, 8), "
//            "HighPrice NUMERIC(18, 8), "
//            "LowPrice NUMERIC(18, 8), "
//            "ClosePrice NUMERIC(18, 8), "
//            "Volume NUMERIC(18, 8), "
//            "CloseTime BIGINT, "
//            "QuoteAssetVolume NUMERIC(18, 8), "
//            "NumberOfTrades INT, "
//            "TakerBuyBaseAssetVolume NUMERIC(18, 8), "
//            "TakerBuyQuoteAssetVolume NUMERIC(18, 8));");
//
//        // Insertar los datos en la tabla temporal
//        for (const auto& row : data) {
//            if (row.size() < 12) {
//                std::cerr << "Insufficient data in row " << row.size() << std::endl;
//                continue;
//            }
//
//            txn.exec_params("INSERT INTO TempData (BaseAssetID, CounterAssetID, IntervalID, EventTimestamp, OpenPrice, HighPrice, LowPrice, ClosePrice, Volume, CloseTime, QuoteAssetVolume, NumberOfTrades, TakerBuyBaseAssetVolume, TakerBuyQuoteAssetVolume) "
//                "VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, $14)",
//                1, 2, 1, std::stoll(row[0]), std::stod(row[1]), std::stod(row[2]), std::stod(row[3]),
//                std::stod(row[4]), std::stod(row[5]), std::stoll(row[6]), std::stod(row[7]),
//                std::stoi(row[8]), std::stod(row[9]), std::stod(row[10]));
//        }
//
//        // Ejecutar el comando INSERT...ON CONFLICT para actualizar o insertar
//        txn.exec("INSERT INTO HistoricalData (BaseAssetID, CounterAssetID, IntervalID, EventTimestamp, EventDateTime, OpenPrice, HighPrice, LowPrice, ClosePrice, Volume, CloseTime, QuoteAssetVolume, NumberOfTrades, TakerBuyBaseAssetVolume, TakerBuyQuoteAssetVolume) "
//            "SELECT BaseAssetID, CounterAssetID, IntervalID, EventTimestamp, "
//            "to_timestamp(EventTimestamp / 1000.0), OpenPrice, HighPrice, LowPrice, ClosePrice, Volume, "
//            "CloseTime, QuoteAssetVolume, NumberOfTrades, TakerBuyBaseAssetVolume, TakerBuyQuoteAssetVolume "
//            "FROM TempData "
//            "ON CONFLICT (EventTimestamp) DO UPDATE SET "
//            "OpenPrice = EXCLUDED.OpenPrice, HighPrice = EXCLUDED.HighPrice, LowPrice = EXCLUDED.LowPrice, "
//            "ClosePrice = EXCLUDED.ClosePrice, Volume = EXCLUDED.Volume, CloseTime = EXCLUDED.CloseTime, "
//            "QuoteAssetVolume = EXCLUDED.QuoteAssetVolume, NumberOfTrades = EXCLUDED.NumberOfTrades, "
//            "TakerBuyBaseAssetVolume = EXCLUDED.TakerBuyBaseAssetVolume, TakerBuyQuoteAssetVolume = EXCLUDED.TakerBuyQuoteAssetVolume;");
//
//        // Confirmar la transacción
//        txn.commit();
//
//        std::cout << "Data successfully inserted or updated in HistoricalData table." << std::endl;
//    }
//    catch (const std::exception& e) {
//        std::cerr << e.what() << std::endl;
//    }
//}
//
//void MarketDataActualizer::actualize(const std::string& symbol, const std::string& interval) {
//    while (Outdated) {
//        //SQLHSTMT hStmt;
//        //SQLAllocHandle(SQL_HANDLE_STMT, hDbc, &hStmt);
//
//        // 1. Obtener el último timestamp de la base de datos
//        SQLBIGINT lastTimestamp = getLastEventTimestamp();
//
//        // 2. Obtener datos históricos desde el último timestamp
//        auto newData = cryptoFetcher.fetchHistoricalData(symbol, interval, lastTimestamp);
//        if (newData.size() < 1000) {
//            Outdated = false;
//        }
//        // 3. Insertar datos en la base de datos
//        if (!newData.empty()) {
//            insertDataIntoDatabase(newData);
//            //std::cout << "Base de datos actualizada con datos recientes." << std::endl;
//        }
//        else {
//            std::cout << "No hay nuevos datos para actualizar." << std::endl;
//        }
//    }
//    Outdated = true;
//}