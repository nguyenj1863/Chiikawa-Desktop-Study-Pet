cmake_minimum_required(VERSION 3.21)

project(ChiikawaStudyPet
    VERSION 1.0.0
    DESCRIPTION "Chiikawa Desktop Study Pet"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if(MSVC)
    add_compile_options(/W4 /permissive- /Zc:__cplusplus /utf-8)
    add_compile_definitions(UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets)

add_executable(ChiikawaStudyPet WIN32
    src/main.cpp
    resources/resources.qrc
)

target_link_libraries(ChiikawaStudyPet PRIVATE Qt6::Widgets)

enable_testing()
