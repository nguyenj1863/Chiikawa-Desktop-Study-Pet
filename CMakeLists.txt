cmake_minimum_required(VERSION 3.21)

# Target macOS 13 (Ventura) and later, so the app runs on the oldest supported macOS.
# Must be set before project() so every target (including the app bundle) uses it.
if(APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "Minimum macOS deployment version")
endif()

project(ChiikawaStudyPet
    VERSION 1.1.0
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

find_package(Qt6 REQUIRED COMPONENTS Widgets Multimedia Test)

set(CHIICAWA_SOURCES
    src/main.cpp
    src/AppSettings.cpp
    src/AnimationManager.cpp
    src/MovementController.cpp
    src/StudyTimer.cpp
    src/TimerBubble.cpp
    src/CompletionToast.cpp
    src/PixelTheme.cpp
    src/AnimatedQuickActionButton.cpp
    src/QuickActionItem.cpp
    src/QuickActionMenu.cpp
    src/QuickActionsEditor.cpp
    src/InstalledAppDiscovery.cpp
    src/ExternalAppLauncher.cpp
    src/SpotifyLauncher.cpp
    src/CollapsibleSection.cpp
    src/AlarmSoundManager.cpp
    src/UISoundManager.cpp
    src/AlarmScheduler.cpp
    src/AlarmNotification.cpp
    src/DesktopLayerHelper.cpp
    src/WindowsIdentity.cpp
    src/OnboardingPreview.cpp
    src/OnboardingWindow.cpp
    src/SettingsPanel.cpp
    src/SystemTrayManager.cpp
    src/StartupManager.cpp
    src/WindowCoordinator.cpp
    src/PetWindow.cpp
    resources/resources.qrc
)

set(CHIICAWA_HEADERS
    include/SettingsDefaults.h
    include/AppSettings.h
    include/AnimationManager.h
    include/MovementController.h
    include/StudyTimer.h
    include/TimerBubble.h
    include/CompletionToast.h
    include/PixelTheme.h
    include/AnimatedQuickActionButton.h
    include/QuickActionItem.h
    include/QuickActionMenu.h
    include/QuickActionsEditor.h
    include/InstalledAppDiscovery.h
    include/ExternalAppLauncher.h
    include/SpotifyLauncher.h
    include/CollapsibleSection.h
    include/AlarmSoundManager.h
    include/UISoundManager.h
    include/AlarmScheduler.h
    include/AlarmNotification.h
    include/DesktopLayerHelper.h
    include/WindowsIdentity.h
    include/OnboardingPreview.h
    include/OnboardingWindow.h
    include/SettingsPanel.h
    include/SystemTrayManager.h
    include/StartupManager.h
    include/WindowCoordinator.h
    include/PetWindow.h
)

add_executable(ChiikawaStudyPet WIN32 MACOSX_BUNDLE
    ${CHIICAWA_SOURCES}
    ${CHIICAWA_HEADERS}
)

target_include_directories(ChiikawaStudyPet PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_definitions(ChiikawaStudyPet PRIVATE
    CHIIKAWA_APP_VERSION="${PROJECT_VERSION}"
    CHIIKAWA_VER_MAJOR=${PROJECT_VERSION_MAJOR}
    CHIIKAWA_VER_MINOR=${PROJECT_VERSION_MINOR}
    CHIIKAWA_VER_PATCH=${PROJECT_VERSION_PATCH}
)

target_link_libraries(ChiikawaStudyPet PRIVATE Qt6::Widgets Qt6::Multimedia)

if(WIN32)
    set_source_files_properties(resources/app.rc PROPERTIES
        OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/chiikawa.ico"
    )
    target_sources(ChiikawaStudyPet PRIVATE resources/app.rc)
    target_link_libraries(ChiikawaStudyPet PRIVATE user32 shell32 ole32 advapi32)
elseif(APPLE)
    enable_language(OBJCXX)
    target_sources(ChiikawaStudyPet PRIVATE
        src/MacWindowHelper.mm
        include/MacWindowHelper.h
        src/ExternalAppLauncher_mac.mm
        src/SpotifyLauncher_mac.mm
    )
    target_link_libraries(ChiikawaStudyPet PRIVATE "-framework AppKit")
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/Info.plist.in"
        "${CMAKE_CURRENT_BINARY_DIR}/Info.plist"
        @ONLY
    )
    set_target_properties(ChiikawaStudyPet PROPERTIES
        MACOSX_BUNDLE TRUE
        OUTPUT_NAME "ChiikawaStudyPet"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_BINARY_DIR}/Info.plist"
    )
endif()

enable_testing()

function(chiikawa_add_test name source)
    add_executable(${name} ${source} ${ARGN})
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(${name} PRIVATE Qt6::Widgets Qt6::Multimedia Qt6::Test)
    if(WIN32)
        add_custom_command(TARGET ${name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:Qt6::Test>"
                "$<TARGET_FILE_DIR:${name}>"
            COMMENT "Ensuring Qt6Test.dll is next to ${name}"
        )
    endif()
    add_test(NAME ${name} COMMAND ${name})
    if(WIN32)
        set_tests_properties(${name} PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
        )
    endif()
endfunction()

chiikawa_add_test(test_study_timer
    tests/test_study_timer.cpp
    src/StudyTimer.cpp
    include/StudyTimer.h
)

chiikawa_add_test(test_app_settings
    tests/test_app_settings.cpp
    src/AppSettings.cpp
    include/AppSettings.h
    include/SettingsDefaults.h
    src/QuickActionItem.cpp
    include/QuickActionItem.h
    src/ExternalAppLauncher.cpp
    include/ExternalAppLauncher.h
    src/PixelTheme.cpp
    include/PixelTheme.h
)
if(WIN32)
    target_link_libraries(test_app_settings PRIVATE user32 shell32 advapi32)
elseif(APPLE)
    target_sources(test_app_settings PRIVATE src/ExternalAppLauncher_mac.mm)
    target_link_libraries(test_app_settings PRIVATE "-framework AppKit")
endif()

chiikawa_add_test(test_animation_manager
    tests/test_animation_manager.cpp
    src/AnimationManager.cpp
    include/AnimationManager.h
    resources/resources.qrc
)

chiikawa_add_test(test_alarm_and_settings
    tests/test_alarm_and_settings.cpp
    src/AppSettings.cpp
    include/AppSettings.h
    include/SettingsDefaults.h
    src/AlarmScheduler.cpp
    include/AlarmScheduler.h
    src/AlarmSoundManager.cpp
    include/AlarmSoundManager.h
    src/UISoundManager.cpp
    include/UISoundManager.h
    src/StudyTimer.cpp
    include/StudyTimer.h
    src/PixelTheme.cpp
    include/PixelTheme.h
    src/CollapsibleSection.cpp
    include/CollapsibleSection.h
    src/CompletionToast.cpp
    include/CompletionToast.h
    src/QuickActionItem.cpp
    include/QuickActionItem.h
    src/QuickActionsEditor.cpp
    include/QuickActionsEditor.h
    src/InstalledAppDiscovery.cpp
    include/InstalledAppDiscovery.h
    src/ExternalAppLauncher.cpp
    include/ExternalAppLauncher.h
    src/SettingsPanel.cpp
    include/SettingsPanel.h
    src/StartupManager.cpp
    include/StartupManager.h
    src/WindowCoordinator.cpp
    include/WindowCoordinator.h
    resources/resources.qrc
)
if(APPLE)
    target_sources(test_alarm_and_settings PRIVATE src/MacWindowHelper.mm include/MacWindowHelper.h src/ExternalAppLauncher_mac.mm)
    target_link_libraries(test_alarm_and_settings PRIVATE "-framework AppKit")
elseif(WIN32)
    target_link_libraries(test_alarm_and_settings PRIVATE user32 shell32 advapi32)
endif()

chiikawa_add_test(test_onboarding
    tests/test_onboarding.cpp
    src/AppSettings.cpp
    include/AppSettings.h
    include/SettingsDefaults.h
    src/AnimationManager.cpp
    include/AnimationManager.h
    src/MovementController.cpp
    include/MovementController.h
    src/StudyTimer.cpp
    include/StudyTimer.h
    src/TimerBubble.cpp
    include/TimerBubble.h
    src/CompletionToast.cpp
    include/CompletionToast.h
    src/PixelTheme.cpp
    include/PixelTheme.h
    src/AnimatedQuickActionButton.cpp
    include/AnimatedQuickActionButton.h
    src/QuickActionMenu.cpp
    include/QuickActionMenu.h
    src/QuickActionItem.cpp
    include/QuickActionItem.h
    src/QuickActionsEditor.cpp
    include/QuickActionsEditor.h
    src/InstalledAppDiscovery.cpp
    include/InstalledAppDiscovery.h
    src/ExternalAppLauncher.cpp
    include/ExternalAppLauncher.h
    src/SpotifyLauncher.cpp
    include/SpotifyLauncher.h
    src/CollapsibleSection.cpp
    include/CollapsibleSection.h
    src/AlarmSoundManager.cpp
    include/AlarmSoundManager.h
    src/UISoundManager.cpp
    include/UISoundManager.h
    src/AlarmScheduler.cpp
    include/AlarmScheduler.h
    src/AlarmNotification.cpp
    include/AlarmNotification.h
    src/DesktopLayerHelper.cpp
    include/DesktopLayerHelper.h
    src/OnboardingPreview.cpp
    include/OnboardingPreview.h
    src/OnboardingWindow.cpp
    include/OnboardingWindow.h
    src/SettingsPanel.cpp
    include/SettingsPanel.h
    src/SystemTrayManager.cpp
    include/SystemTrayManager.h
    src/StartupManager.cpp
    include/StartupManager.h
    src/WindowCoordinator.cpp
    include/WindowCoordinator.h
    src/PetWindow.cpp
    include/PetWindow.h
    resources/resources.qrc
)
if(WIN32)
    target_link_libraries(test_onboarding PRIVATE user32 shell32 advapi32)
elseif(APPLE)
    target_sources(test_onboarding PRIVATE
        src/MacWindowHelper.mm
        include/MacWindowHelper.h
        src/ExternalAppLauncher_mac.mm
        src/SpotifyLauncher_mac.mm
    )
    target_link_libraries(test_onboarding PRIVATE "-framework AppKit")
endif()

chiikawa_add_test(test_quick_actions
    tests/test_quick_actions.cpp
    src/AppSettings.cpp
    include/AppSettings.h
    include/SettingsDefaults.h
    src/AnimationManager.cpp
    include/AnimationManager.h
    src/MovementController.cpp
    include/MovementController.h
    src/StudyTimer.cpp
    include/StudyTimer.h
    src/TimerBubble.cpp
    include/TimerBubble.h
    src/CompletionToast.cpp
    include/CompletionToast.h
    src/PixelTheme.cpp
    include/PixelTheme.h
    src/AnimatedQuickActionButton.cpp
    include/AnimatedQuickActionButton.h
    src/QuickActionMenu.cpp
    include/QuickActionMenu.h
    src/QuickActionItem.cpp
    include/QuickActionItem.h
    src/QuickActionsEditor.cpp
    include/QuickActionsEditor.h
    src/InstalledAppDiscovery.cpp
    include/InstalledAppDiscovery.h
    src/ExternalAppLauncher.cpp
    include/ExternalAppLauncher.h
    src/SpotifyLauncher.cpp
    include/SpotifyLauncher.h
    src/CollapsibleSection.cpp
    include/CollapsibleSection.h
    src/AlarmSoundManager.cpp
    include/AlarmSoundManager.h
    src/UISoundManager.cpp
    include/UISoundManager.h
    src/AlarmScheduler.cpp
    include/AlarmScheduler.h
    src/AlarmNotification.cpp
    include/AlarmNotification.h
    src/DesktopLayerHelper.cpp
    include/DesktopLayerHelper.h
    src/OnboardingPreview.cpp
    include/OnboardingPreview.h
    src/OnboardingWindow.cpp
    include/OnboardingWindow.h
    src/SettingsPanel.cpp
    include/SettingsPanel.h
    src/SystemTrayManager.cpp
    include/SystemTrayManager.h
    src/StartupManager.cpp
    include/StartupManager.h
    src/WindowCoordinator.cpp
    include/WindowCoordinator.h
    src/PetWindow.cpp
    include/PetWindow.h
    resources/resources.qrc
)
if(WIN32)
    target_link_libraries(test_quick_actions PRIVATE user32 shell32 advapi32)
elseif(APPLE)
    target_sources(test_quick_actions PRIVATE
        src/MacWindowHelper.mm
        include/MacWindowHelper.h
        src/ExternalAppLauncher_mac.mm
        src/SpotifyLauncher_mac.mm
    )
    target_link_libraries(test_quick_actions PRIVATE "-framework AppKit")
endif()

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
