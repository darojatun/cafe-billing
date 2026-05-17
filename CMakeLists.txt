cmake_minimum_required(VERSION 3.14)
project(CafeBilling VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK3 REQUIRED gtk+-3.0)
pkg_check_modules(SQLITE3 REQUIRED sqlite3)

include_directories(
    ${GTK3_INCLUDE_DIRS}
    ${SQLITE3_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}
)

link_directories(
    ${GTK3_LIBRARY_DIRS}
    ${SQLITE3_LIBRARY_DIRS}
)

# ── Server ──────────────────────────────────────────────────────────────────
add_executable(cafe-server
    server/main.cpp
    server/Database.cpp
    server/BillingServer.cpp
    server/MainWindow.cpp
    server/Receipt.cpp
)

target_compile_options(cafe-server PRIVATE ${GTK3_CFLAGS_OTHER})
target_link_libraries(cafe-server
    ${GTK3_LIBRARIES}
    ${SQLITE3_LIBRARIES}
)

# ── Client ──────────────────────────────────────────────────────────────────
add_executable(cafe-client
    client/main.cpp
    client/TcpClient.cpp
    client/ClientWindow.cpp
)

target_compile_options(cafe-client PRIVATE ${GTK3_CFLAGS_OTHER})
target_link_libraries(cafe-client
    ${GTK3_LIBRARIES}
)
