// #include "Database.h"
// #include <iostream>
// #include <chrono>

// Database::Database() {

// }


// void Database::start() {
//     dbThread = std::thread(&Database::dbUpdateLoop, this);
// }

// void Database::stop() {
//     running = false;
//     if (dbThread.joinable()) {
//         dbThread.join();
//     }
// }

// void Database::update() {
//     db.actualize();
//     // C�digo para actualizar la base de datos
// }

// void Database::dbUpdateLoop() {
//     while (running) {
//         update();
//         std::this_thread::sleep_for(std::chrono::milliseconds(500)); // Ajustar seg�n necesidad
//     }
// }