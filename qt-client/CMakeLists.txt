cmake_minimum_required(VERSION 3.21)

project(RunBayClient VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

foreach(CONFIG_TYPE DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${CONFIG_TYPE} "${CMAKE_BINARY_DIR}/bin")
endforeach()

find_package(Qt6 REQUIRED COMPONENTS Widgets Network)

qt_add_executable(runbay-client
    src/main.cpp
    src/ApiClient.cpp
    src/ApiClient.h
    src/MainWindow.cpp
    src/MainWindow.h
    src/Task.h
    src/TaskTableModel.cpp
    src/TaskTableModel.h
    resources/runbay.qrc
)

target_link_libraries(runbay-client PRIVATE Qt6::Widgets Qt6::Network)

if(WIN32)
    target_sources(runbay-client PRIVATE resources/runbay.rc)
    set_target_properties(runbay-client PROPERTIES WIN32_EXECUTABLE TRUE)
    target_link_libraries(runbay-client PRIVATE ole32 uuid)
endif()
