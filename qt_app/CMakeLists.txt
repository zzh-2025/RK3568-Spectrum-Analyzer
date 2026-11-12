cmake_minimum_required(VERSION 3.16)
project(SpectrumQtApp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

find_package(Qt5 5.12 REQUIRED COMPONENTS Widgets Charts)

add_executable(spectrum_qt
    main.cpp
    mainwindow.cpp
    mainwindow.h
    batchmanager.cpp
    batchmanager.h
    spectrumchart.cpp
    spectrumchart.h
    csvutils.cpp
    csvutils.h
    ../src/coupling_analysis.cpp
)

target_include_directories(spectrum_qt PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../include"
)

target_link_libraries(spectrum_qt PRIVATE Qt5::Widgets Qt5::Charts)
target_compile_options(spectrum_qt PRIVATE -Wall -Wextra -Wpedantic)
