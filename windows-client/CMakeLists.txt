cmake_minimum_required(VERSION 3.14)
project(CafeBillWindows VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Keep the Windows client compatible with older x86-64 CPUs.
add_compile_options(-march=x86-64 -mtune=generic)
add_link_options(-march=x86-64 -mtune=generic)

# ── Windows Client (cross-compiled or native MSVC/MinGW) ───────────────────
add_executable(cafe-client-win WIN32
    main.cpp
    WsClient.cpp
)

target_link_libraries(cafe-client-win
    ws2_32
    comctl32
)

target_compile_definitions(cafe-client-win PRIVATE
    WIN32_LEAN_AND_MEAN
    _WIN32_WINNT=0x0601
    UNICODE
)
