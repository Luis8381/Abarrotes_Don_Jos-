cmake_minimum_required(VERSION 3.16)

project(AbarroteraDonJose LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_PREFIX_PATH "C:/QtDesigner/6.12.0/mingw_64")

find_package(Qt6 REQUIRED COMPONENTS Widgets)

set(MARIADB_ROOT "C:/Program Files/MariaDB/MariaDB Connector C 64-bit")

add_executable(AbarroteraDonJose
    main.cpp
)

target_include_directories(AbarroteraDonJose PRIVATE
    "${MARIADB_ROOT}/include"
)

target_link_libraries(AbarroteraDonJose PRIVATE
    Qt6::Widgets
    "${CMAKE_CURRENT_SOURCE_DIR}/mariadb/libmariadb.dll.a"
)

add_custom_command(TARGET AbarroteraDonJose POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/mariadb/libmariadb.dll"
    "$<TARGET_FILE_DIR:AbarroteraDonJose>"
)

add_custom_command(TARGET AbarroteraDonJose POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/estilo_claro.qss"
        "$<TARGET_FILE_DIR:AbarroteraDonJose>"
)