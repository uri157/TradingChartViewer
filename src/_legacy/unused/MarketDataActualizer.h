#pragma once
//#ifndef MARKETDATAACTUALIZER_H
//#define MARKETDATAACTUALIZER_H
//#include "DatabaseConnection.h"
//#include "CryptoDataFetcher.h"
//
//class MarketDataActualizer : public DatabaseConnection {
//	bool Outdated = true;
//	CryptoDataFetcher cryptoFetcher;
//	void fetchAndInsertData(const std::string& symbol, const std::string& interval);
//	void insertDataIntoDatabase(const std::vector<std::vector<std::string>>& data);
//public:
//	void actualize(const std::string& symbol = "BTCUSDT", const std::string& interval = "1m");
//};
//
//#endif