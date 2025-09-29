// BinaryToTextConverter.h
#pragma once

#include <string>
#include <vector>

#include "infra/storage/PriceData.h"  // Ensure this header is available

namespace infra::tools {

class BinaryToTextConverter {
public:
    // Constructor
    // inputBinaryFile: Ruta al archivo binario de entrada
    // outputTextFile: Ruta al archivo de texto de salida
    // maxRecords: Numero maximo de registros a copiar (default 5000)
    BinaryToTextConverter(const std::string& inputBinaryFile,
                          const std::string& outputTextFile,
                          size_t maxRecords = 5000);

    // Metodo para realizar la conversion
    bool convert();

private:
    std::string inputBinaryFile;
    std::string outputTextFile;
    size_t maxRecords;

    // Funcion auxiliar para convertir timestamp a formato legible
    std::string formatTimestamp(long long timestamp) const;
};

}  // namespace infra::tools
