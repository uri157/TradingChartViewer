// BinaryToTextConverter.cpp
#define _CRT_SECURE_NO_WARNINGS
#include "infra/tools/BinaryToTextConverter.h"

#include <algorithm>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>

namespace infra::tools {

BinaryToTextConverter::BinaryToTextConverter(const std::string& inputBinaryFile,
                                             const std::string& outputTextFile,
                                             size_t maxRecords)
    : inputBinaryFile(inputBinaryFile),
      outputTextFile(outputTextFile),
      maxRecords(maxRecords) {
}

std::string BinaryToTextConverter::formatTimestamp(long long timestamp) const {
    // Asume que el timestamp esta en milisegundos
    std::time_t timeInSeconds = static_cast<std::time_t>(timestamp / 1000);
    std::tm* tmPtr = std::gmtime(&timeInSeconds);
    if (tmPtr == nullptr) {
        return "Invalid Time";
    }

    char buffer[30];
    if (std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S UTC", tmPtr)) {
        return std::string(buffer);
    } else {
        return "Invalid Time";
    }
}

bool BinaryToTextConverter::convert() {
    // Abrir el archivo binario para lectura
    std::ifstream binFile(inputBinaryFile, std::ios::binary | std::ios::ate);
    if (!binFile.is_open()) {
        std::cerr << "Error: No se pudo abrir el archivo binario: " << inputBinaryFile << std::endl;
        return false;
    }

    // Obtener el tamano del archivo
    std::streamsize fileSize = binFile.tellg();
    binFile.seekg(0, std::ios::beg);

    // Calcular el numero total de registros
    size_t totalRecords = static_cast<size_t>(fileSize) / sizeof(infra::storage::PriceData);
    if (totalRecords == 0) {
        std::cerr << "Error: El archivo binario esta vacio." << std::endl;
        return false;
    }

    // Determinar cuantos registros copiar
    size_t recordsToCopy = std::min(maxRecords, totalRecords);

    // Calcular la posicion de inicio para leer los ultimos 'recordsToCopy' registros
    std::streampos startPos = static_cast<std::streampos>((totalRecords - recordsToCopy) * sizeof(infra::storage::PriceData));
    binFile.seekg(startPos, std::ios::beg);

    // Leer los registros
    std::vector<infra::storage::PriceData> records(recordsToCopy);
    if (!binFile.read(reinterpret_cast<char*>(records.data()), recordsToCopy * sizeof(infra::storage::PriceData))) {
        std::cerr << "Error: No se pudieron leer los registros del archivo binario." << std::endl;
        return false;
    }

    binFile.close();

    // Ordenar los registros de mas reciente a mas antiguo
    std::reverse(records.begin(), records.end());

    // Abrir el archivo de texto para escritura
    std::ofstream txtFile(outputTextFile);
    if (!txtFile.is_open()) {
        std::cerr << "Error: No se pudo abrir el archivo de texto: " << outputTextFile << std::endl;
        return false;
    }

    // Escribir encabezados
    txtFile << "OpenTime\t\tDate\t\t\tClosePrice\n";
    txtFile << "-------------------------------------------------------------\n";

    // Escribir los registros
    for (const auto& record : records) {
        txtFile << record.openTime << "\t\t"
                << formatTimestamp(record.openTime) << "\t"
                << record.closePrice << "\n";
    }

    txtFile.close();

    std::cout << "Conversion completada exitosamente. Datos guardados en: " << outputTextFile << std::endl;
    return true;
}

}  // namespace infra::tools
