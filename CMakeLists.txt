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
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets Test)

set(CHIICAWA_SOURCES
    src/main.cpp
    src/AppSettings.cpp
    src/AnimationManager.cpp
    src/MovementController.cpp
    src/StudyTimer.cpp
    src/TimerBubble.cpp
    src/SettingsPanel.cpp
    src/SystemTrayManager.cpp
    src/StartupManager.cpp
    src/PetWindow.cpp
    resources/resources.qrc
)

set(CHIICAWA_HEADERS
    include/AppSettings.h
    include/AnimationManager.h
    include/MovementController.h
    include/StudyTimer.h
    include/TimerBubble.h
    include/SettingsPanel.h
    include/SystemTrayManager.h
    include/StartupManager.h
    include/PetWindow.h
)

add_executable(ChiikawaStudyPet WIN32
    ${CHIICAWA_SOURCES}
    ${CHIICAWA_HEADERS}
)

target_include_directories(ChiikawaStudyPet PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(ChiikawaStudyPet PRIVATE Qt6::Widgets)

if(WIN32)
    target_link_libraries(ChiikawaStudyPet PRIVATE user32)
endif()

enable_testing()

function(chiikawa_add_test name source)
    add_executable(${name} ${source} ${ARGN})
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(${name} PRIVATE Qt6::Widgets Qt6::Test)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

chiikawa_add_test(test_study_timer
    tests/test_study_timer.cpp
    src/StudyTimer.cpp
)

chiikawa_add_test(test_app_settings
    tests/test_app_settings.cpp
    src/AppSettings.cpp
)

chiikawa_add_test(test_animation_manager
    tests/test_animation_manager.cpp
    src/AnimationManager.cpp
    resources/resources.qrc
)

if(WIN32)
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS
        "${Qt6_DIR}/../../../bin"
        "$ENV{QTDIR}/bin"
    )
    if(WINDEPLOYQT_EXECUTABLE)
        add_custom_target(deploy
            COMMAND "${WINDEPLOYQT_EXECUTABLE}"
                --release
                --no-translations
                "$<TARGET_FILE:ChiikawaStudyPet>"
            DEPENDS ChiikawaStudyPet
            COMMENT "Running windeployqt for ChiikawaStudyPet"
        )
    endif()
endif()
