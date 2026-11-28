cmake_minimum_required(VERSION 3.21)

# Target macOS 13 (Ventura) and later, so the app runs on the oldest supported macOS.
# Must be set before project() so every target (including the app bundle) uses it.
if(APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "Minimum macOS deployment version")
endif()

project(ChiikawaStudyPet
    VERSION 1.2.0
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

if(APPLE)
    enable_language(OBJCXX)
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets Multimedia Network Sql Test)
# QtPdf powers PDF import in the Study Library. Without it the app still builds;
# PDF import reports that support was not built and PPTX/text import keep working.
find_package(Qt6 QUIET OPTIONAL_COMPONENTS Pdf)

# -----------------------------------------------------------------------------
# Sources grouped by module (headers live under include/<module>/)
# -----------------------------------------------------------------------------
set(CHIIKAWA_APP_SOURCES
    src/app/AppSettings.cpp
    src/app/SystemTrayManager.cpp
    src/app/WindowCoordinator.cpp
    include/app/AppSettings.h
    include/app/SettingsDefaults.h
    include/app/SystemTrayManager.h
    include/app/WindowCoordinator.h
)

set(CHIIKAWA_PET_SOURCES
    src/pet/AnimationManager.cpp
    src/pet/MovementController.cpp
    src/pet/PetWindow.cpp
    include/pet/AnimationManager.h
    include/pet/MovementController.h
    include/pet/PetWindow.h
)

set(CHIIKAWA_QUICKACTIONS_SOURCES
    src/quickactions/AnimatedQuickActionButton.cpp
    src/quickactions/ExternalAppLauncher.cpp
    src/quickactions/InstalledAppDiscovery.cpp
    src/quickactions/QuickActionItem.cpp
    src/quickactions/QuickActionMenu.cpp
    src/quickactions/QuickActionsEditor.cpp
    src/quickactions/SpotifyLauncher.cpp
    include/quickactions/AnimatedQuickActionButton.h
    include/quickactions/ExternalAppLauncher.h
    include/quickactions/InstalledAppDiscovery.h
    include/quickactions/QuickActionItem.h
    include/quickactions/QuickActionMenu.h
    include/quickactions/QuickActionsEditor.h
    include/quickactions/SpotifyLauncher.h
)

set(CHIIKAWA_TIMER_SOURCES
    src/timer/CompletionToast.cpp
    src/timer/StudyTimer.cpp
    src/timer/TimerBubble.cpp
    include/timer/CompletionToast.h
    include/timer/StudyTimer.h
    include/timer/TimerBubble.h
)

set(CHIIKAWA_ALARM_SOURCES
    src/alarm/AlarmNotification.cpp
    src/alarm/AlarmScheduler.cpp
    src/alarm/AlarmSoundManager.cpp
    src/alarm/UISoundManager.cpp
    include/alarm/AlarmNotification.h
    include/alarm/AlarmScheduler.h
    include/alarm/AlarmSoundManager.h
    include/alarm/UISoundManager.h
)

set(CHIIKAWA_AI_SOURCES
    src/ai/OllamaClient.cpp
    include/ai/LlmClient.h
    include/ai/OllamaClient.h
)

# Study: question model, library storage and retrieval
set(CHIIKAWA_STUDY_CORE_SOURCES
    src/study/Retriever.cpp
    src/study/StudyDatabase.cpp
    src/study/StudyLibrary.cpp
    include/study/Question.h
    include/study/Retriever.h
    include/study/StudyDatabase.h
    include/study/StudyLibrary.h
    include/study/StudyTypes.h
)

# Study: reading documents (text, OCR, figures)
set(CHIIKAWA_STUDY_INGEST_SOURCES
    src/study/ingest/Chunker.cpp
    src/study/ingest/DocumentExtractor.cpp
    src/study/ingest/FigureInterpreter.cpp
    src/study/ingest/OcrEngine.cpp
    src/study/ingest/ZipReader.cpp
    include/study/ingest/Chunker.h
    include/study/ingest/DocumentExtractor.h
    include/study/ingest/FigureInterpreter.h
    include/study/ingest/OcrEngine.h
    include/study/ingest/ZipReader.h
)

# Study: writing and checking questions
set(CHIIKAWA_STUDY_GENERATION_SOURCES
    src/study/generation/QuestionGenerator.cpp
    src/study/generation/QuestionValidator.cpp
    include/study/generation/QuestionGenerator.h
    include/study/generation/QuestionValidator.h
)

# Study: grading answers
set(CHIIKAWA_STUDY_EVALUATION_SOURCES
    src/study/evaluation/AnswerNormalizer.cpp
    src/study/evaluation/ChoiceEvaluators.cpp
    src/study/evaluation/WrittenAnswerEvaluator.cpp
    include/study/evaluation/AnswerNormalizer.h
    include/study/evaluation/ChoiceEvaluators.h
    include/study/evaluation/Evaluation.h
    include/study/evaluation/WrittenAnswerEvaluator.h
)

# Study: choosing and asking questions
set(CHIIKAWA_STUDY_PRACTICE_SOURCES
    src/study/practice/QuestionScheduler.cpp
    src/study/practice/QuestionSelector.cpp
    src/study/practice/QuizController.cpp
    src/study/practice/SetQuestionProvider.cpp
    include/study/practice/QuestionProvider.h
    include/study/practice/QuestionScheduler.h
    include/study/practice/QuestionSelector.h
    include/study/practice/QuizController.h
    include/study/practice/SetQuestionProvider.h
)

# Study: progress and spaced repetition
set(CHIIKAWA_STUDY_PROGRESS_SOURCES
    src/study/progress/ProgressTracker.cpp
    src/study/progress/ReviewScheduler.cpp
    src/study/progress/ScheduleStore.cpp
    include/study/progress/ProgressTracker.h
    include/study/progress/ReviewScheduler.h
    include/study/progress/ScheduleStore.h
)

set(CHIIKAWA_STUDY_SOURCES
    ${CHIIKAWA_STUDY_CORE_SOURCES}
    ${CHIIKAWA_STUDY_INGEST_SOURCES}
    ${CHIIKAWA_STUDY_GENERATION_SOURCES}
    ${CHIIKAWA_STUDY_EVALUATION_SOURCES}
    ${CHIIKAWA_STUDY_PRACTICE_SOURCES}
    ${CHIIKAWA_STUDY_PROGRESS_SOURCES}
)

set(CHIIKAWA_UI_SOURCES
    src/ui/CollapsibleSection.cpp
    src/ui/NotificationStack.cpp
    src/ui/OnboardingPreview.cpp
    src/ui/OnboardingWindow.cpp
    src/ui/PixelTheme.cpp
    src/ui/SettingsPanel.cpp
    src/ui/PixelSegmentedControl.cpp
    include/ui/CollapsibleSection.h
    include/ui/NotificationStack.h
    include/ui/OnboardingPreview.h
    include/ui/OnboardingWindow.h
    include/ui/PixelTheme.h
    include/ui/SettingsPanel.h
    include/ui/PixelSegmentedControl.h
)

# Study App windows and widgets (shared widgets stay in ui/)
set(CHIIKAWA_STUDYUI_SOURCES
    src/studyui/QuestionEditor.cpp
    src/studyui/QuizPopup.cpp
    src/studyui/SetQuestionsView.cpp
    src/studyui/StudyLibraryWindow.cpp
    src/studyui/StudyPracticeView.cpp
    src/studyui/StudyPreferencesPage.cpp
    src/studyui/StudySetList.cpp
    src/studyui/SupportView.cpp
    include/studyui/QuestionEditor.h
    include/studyui/QuizPopup.h
    include/studyui/SetQuestionsView.h
    include/studyui/StudyLibraryWindow.h
    include/studyui/StudyPracticeView.h
    include/studyui/StudyPreferencesPage.h
    include/studyui/StudySetList.h
    include/studyui/StudyUiHelpers.h
    include/studyui/SupportView.h
)

set(CHIIKAWA_PLATFORM_SOURCES
    src/platform/DesktopLayerHelper.cpp
    src/platform/StartupManager.cpp
    src/platform/windows/WindowsIdentity.cpp
    src/platform/windows/WindowsOcr.cpp
    include/platform/DesktopLayerHelper.h
    include/platform/StartupManager.h
    include/platform/WindowsIdentity.h
)

set(CHIIKAWA_MACOS_SOURCES
    src/platform/macos/ExternalAppLauncher_mac.mm
    src/platform/macos/MacWindowHelper.mm
    src/platform/macos/SpotifyLauncher_mac.mm
    src/platform/macos/VisionOcr.mm
    include/platform/MacWindowHelper.h
)

# -----------------------------------------------------------------------------
# Shared implementation used by the application and the test executables
# -----------------------------------------------------------------------------
add_library(chiikawa_core STATIC
    ${CHIIKAWA_APP_SOURCES}
    ${CHIIKAWA_PET_SOURCES}
    ${CHIIKAWA_QUICKACTIONS_SOURCES}
    ${CHIIKAWA_TIMER_SOURCES}
    ${CHIIKAWA_ALARM_SOURCES}
    ${CHIIKAWA_AI_SOURCES}
    ${CHIIKAWA_STUDY_SOURCES}
    ${CHIIKAWA_UI_SOURCES}
    ${CHIIKAWA_STUDYUI_SOURCES}
    ${CHIIKAWA_PLATFORM_SOURCES}
)

target_include_directories(chiikawa_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(chiikawa_core PUBLIC Qt6::Widgets Qt6::Multimedia Qt6::Network Qt6::Sql)
if(TARGET Qt6::Pdf)
    target_link_libraries(chiikawa_core PUBLIC Qt6::Pdf)
    target_compile_definitions(chiikawa_core PUBLIC CHIIKAWA_HAS_QTPDF=1)
    message(STATUS "QtPdf found: PDF import enabled")
else()
    message(STATUS "QtPdf not found: PDF import disabled")
endif()

if(WIN32)
    # windowsapp: Windows Runtime APIs (Windows.Media.Ocr for reading scanned pages)
    target_link_libraries(chiikawa_core PUBLIC user32 shell32 ole32 advapi32 version windowsapp)
elseif(APPLE)
    target_sources(chiikawa_core PRIVATE ${CHIIKAWA_MACOS_SOURCES})
    target_link_libraries(chiikawa_core PUBLIC "-framework AppKit" "-framework Vision")
endif()

# -----------------------------------------------------------------------------
# Application
# -----------------------------------------------------------------------------
add_executable(ChiikawaStudyPet WIN32 MACOSX_BUNDLE
    src/app/main.cpp
    resources/resources.qrc
)

target_compile_definitions(ChiikawaStudyPet PRIVATE
    CHIIKAWA_APP_VERSION="${PROJECT_VERSION}"
    CHIIKAWA_VER_MAJOR=${PROJECT_VERSION_MAJOR}
    CHIIKAWA_VER_MINOR=${PROJECT_VERSION_MINOR}
    CHIIKAWA_VER_PATCH=${PROJECT_VERSION_PATCH}
)

target_link_libraries(ChiikawaStudyPet PRIVATE chiikawa_core)

if(WIN32)
    set_source_files_properties(resources/app.rc PROPERTIES
        OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/chiikawa.ico"
    )
    target_sources(ChiikawaStudyPet PRIVATE resources/app.rc)
elseif(APPLE)
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

# -----------------------------------------------------------------------------
# Tests
# -----------------------------------------------------------------------------
enable_testing()

function(chiikawa_add_test name source)
    add_executable(${name} ${source} ${ARGN})
    target_link_libraries(${name} PRIVATE chiikawa_core Qt6::Test)
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

chiikawa_add_test(test_study_timer tests/test_study_timer.cpp)
chiikawa_add_test(test_app_settings tests/test_app_settings.cpp)
chiikawa_add_test(test_animation_manager tests/test_animation_manager.cpp resources/resources.qrc)
chiikawa_add_test(test_alarm_and_settings tests/test_alarm_and_settings.cpp resources/resources.qrc)
chiikawa_add_test(test_onboarding tests/test_onboarding.cpp resources/resources.qrc)
chiikawa_add_test(test_quick_actions tests/test_quick_actions.cpp resources/resources.qrc)
chiikawa_add_test(test_checkins tests/test_checkins.cpp resources/resources.qrc)
chiikawa_add_test(test_study_extract tests/test_study_extract.cpp tests/StudyTestUtils.h)
chiikawa_add_test(test_study_pipeline tests/test_study_pipeline.cpp tests/StudyTestUtils.h)
chiikawa_add_test(test_study_migration tests/test_study_migration.cpp)
chiikawa_add_test(test_review_scheduler tests/test_review_scheduler.cpp)
chiikawa_add_test(test_ollama_client tests/test_ollama_client.cpp)
chiikawa_add_test(test_study_generation tests/test_study_generation.cpp tests/MockLlmClient.h)
chiikawa_add_test(test_study_answers tests/test_study_answers.cpp tests/MockLlmClient.h)
chiikawa_add_test(test_study_support tests/test_study_support.cpp tests/MockLlmClient.h tests/StudyTestUtils.h)
chiikawa_add_test(test_study_ocr tests/test_study_ocr.cpp tests/MockLlmClient.h tests/StudyTestUtils.h)
chiikawa_add_test(test_study_figures tests/test_study_figures.cpp tests/MockLlmClient.h)
chiikawa_add_test(test_study_ui tests/test_study_ui.cpp tests/StudyTestUtils.h tests/MockLlmClient.h resources/resources.qrc)

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
