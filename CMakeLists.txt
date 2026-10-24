# CMakeLists.txt
cmake_minimum_required(VERSION 3.20)

# ==============================================================================
# Извлечение версии из Git-тегов и общего числа коммитов по всем веткам
# ==============================================================================
find_package(Git QUIET)

set(GIT_TAG_VER "0.0.0")
set(GIT_COMMIT_COUNT "0")

if(GIT_FOUND AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git")
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" describe --tags --abbrev=0
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        OUTPUT_VARIABLE RAW_GIT_TAG
        ERROR_QUIET
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(RAW_GIT_TAG MATCHES "^[vV]?([0-9]+\\.[0-9]+\\.[0-9]+)")
        set(GIT_TAG_VER "${CMAKE_MATCH_1}")
    elseif(RAW_GIT_TAG MATCHES "^[vV]?([0-9]+\\.[0-9]+)")
        set(GIT_TAG_VER "${CMAKE_MATCH_1}.0")
    endif()

    execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-list --all --count
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        OUTPUT_VARIABLE RAW_COMMIT_COUNT
        ERROR_QUIET
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(RAW_COMMIT_COUNT MATCHES "^[0-9]+$")
        set(GIT_COMMIT_COUNT "${RAW_COMMIT_COUNT}")
    endif()
endif()

set(AIDE_FULL_VERSION "${GIT_TAG_VER}.${GIT_COMMIT_COUNT}")
message(STATUS "AIDE Version resolved from Git: ${AIDE_FULL_VERSION}")

project(AIDE
    VERSION ${AIDE_FULL_VERSION}
    DESCRIPTION "AI Development Environment - Multi-Document Qt6 Code Editor"
    HOMEPAGE_URL "https://github.com/mistificator/aide"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network)

if(APPLE AND TARGET WrapOpenGL::WrapOpenGL)
    get_target_property(_gl_libs WrapOpenGL::WrapOpenGL INTERFACE_LINK_LIBRARIES)
    if(_gl_libs)
        list(FILTER _gl_libs EXCLUDE REGEX "AGL")
        set_target_properties(WrapOpenGL::WrapOpenGL PROPERTIES INTERFACE_LINK_LIBRARIES "${_gl_libs}")
    endif()
endif()

include(GNUInstallDirs)

# ==============================================================================
# Генерация иконок приложения (aide.ico и aide.svg) для ресурсов и пакета CPack
# ==============================================================================
set(AIDE_GENERATED_ICO "${CMAKE_CURRENT_BINARY_DIR}/aide.ico")
set(AIDE_GENERATED_SVG "${CMAKE_CURRENT_BINARY_DIR}/aide.svg")

add_executable(aide_icon_gen src/tools/IconGenerator.cpp)
set_target_properties(aide_icon_gen PROPERTIES AUTOMOC OFF AUTORCC OFF AUTOUIC OFF)

add_custom_command(
    OUTPUT "${AIDE_GENERATED_ICO}" "${AIDE_GENERATED_SVG}"
    COMMAND aide_icon_gen "${AIDE_GENERATED_ICO}" "${AIDE_GENERATED_SVG}"
    DEPENDS aide_icon_gen
    COMMENT "Generating AIDE multi-resolution .ico and .svg icons..."
)
add_custom_target(aide_icons ALL DEPENDS "${AIDE_GENERATED_ICO}" "${AIDE_GENERATED_SVG}")

set(AIDE_PLATFORM_SOURCES)
if(WIN32)
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/src/version.rc.in"
        "${CMAKE_CURRENT_BINARY_DIR}/version.rc"
        @ONLY
    )
    set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/version.rc"
        PROPERTIES OBJECT_DEPENDS "${AIDE_GENERATED_ICO}"
    )
    list(APPEND AIDE_PLATFORM_SOURCES "${CMAKE_CURRENT_BINARY_DIR}/version.rc")
endif()

add_executable(aide WIN32 MACOSX_BUNDLE
    src/main.cpp
    src/core/ConfigManager.h
    src/core/ConfigManager.cpp
    src/core/ProjectManager.h
    src/core/ProjectManager.cpp
    src/syntax/ColorScheme.h
    src/syntax/ColorScheme.cpp
    src/syntax/LexicalHighlighter.h
    src/syntax/LexicalHighlighter.cpp
    src/syntax/ThemeAndSyntaxDownloader.h
    src/syntax/ThemeAndSyntaxDownloader.cpp
    src/editor/CodeEditor.h
    src/editor/CodeEditor.cpp
    src/editor/EditorTabWidget.h
    src/editor/EditorTabWidget.cpp
    src/ai/AiProvider.h
    src/ai/AiProvider.cpp
    src/ai/AiTaskQueue.h
    src/ai/AiTaskQueue.cpp
    src/ai/SummaryWorker.h
    src/ai/SummaryWorker.cpp
    src/ui/ContextInspectorDock.h
    src/ui/ContextInspectorDock.cpp
    src/ui/AiPreferencesDialog.h
    src/ui/AiPreferencesDialog.cpp
    src/ui/EditorPreferencesDialog.h
    src/ui/EditorPreferencesDialog.cpp
    src/ui/DiffViewerDialog.h
    src/ui/DiffViewerDialog.cpp
    src/ui/AiDebugConsoleDock.h
    src/ui/AiDebugConsoleDock.cpp
    src/ui/AiTaskQueueDock.h
    src/ui/AiTaskQueueDock.cpp
    src/ui/MainWindow.h
    src/ui/MainWindow.cpp
    src/ui/AppIcon.h
    src/ui/AppIcon.cpp
    src/ui/RecursiveSummaryDialog.h
    src/ui/RecursiveSummaryDialog.cpp
    src/ui/MarkdownRenderer.h
    src/ui/MarkdownRenderer.cpp
    src/ui/PromptHistoryDock.h
    src/ui/PromptHistoryDock.cpp

    ${AIDE_PLATFORM_SOURCES}

    .github/workflows/ci.yml
)

add_dependencies(aide aide_icons)

target_compile_definitions(aide PRIVATE
    AIDE_VERSION_STRING="${PROJECT_VERSION}"
)

target_include_directories(aide PRIVATE src "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(aide PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network)

set_target_properties(aide PROPERTIES
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.aide.editor"
    MACOSX_BUNDLE_BUNDLE_NAME "AIDE"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}"
)

install(TARGETS aide
    BUNDLE  DESTINATION . COMPONENT Runtime
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT Runtime
)

if(UNIX AND NOT APPLE)
    set(AIDE_DESKTOP_FILE "${CMAKE_CURRENT_BINARY_DIR}/org.aide.editor.desktop")
    file(WRITE "${AIDE_DESKTOP_FILE}"
"[Desktop Entry]\n"
"Type=Application\n"
"Name=AIDE\n"
"GenericName=AI Code Editor\n"
"Comment=${PROJECT_DESCRIPTION}\n"
"Exec=aide %F\n"
"Icon=aide\n"
"Terminal=false\n"
"Categories=Development;IDE;TextEditor;\n"
    )
    install(FILES "${AIDE_GENERATED_SVG}"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps"
        COMPONENT Runtime
    )
    install(FILES "${AIDE_DESKTOP_FILE}"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/applications"
        COMPONENT Runtime
    )
endif()

if((WIN32 OR APPLE) AND COMMAND qt_generate_deploy_app_script)
    if(Qt6_VERSION VERSION_GREATER_EQUAL "6.5.0")
        qt_generate_deploy_app_script(
            TARGET aide
            OUTPUT_SCRIPT deploy_script
            NO_UNSUPPORTED_PLATFORM_ERROR
        )
    else()
        qt_generate_deploy_app_script(
            TARGET aide
            FILENAME_VARIABLE deploy_script
            NO_UNSUPPORTED_PLATFORM_ERROR
        )
    endif()
    install(SCRIPT ${deploy_script} COMPONENT Runtime)
endif()

# ==============================================================================
# Конфигурация CPack
# ==============================================================================
set(CPACK_PACKAGE_NAME "aide")
set(CPACK_PACKAGE_VENDOR "AIDE Project")
set(CPACK_PACKAGE_HOMEPAGE_URL "${PROJECT_HOMEPAGE_URL}")
set(CPACK_PACKAGE_CONTACT "AIDE Project <${PROJECT_HOMEPAGE_URL}>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VERSION_MAJOR "${PROJECT_VERSION_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR "${PROJECT_VERSION_MINOR}")
set(CPACK_PACKAGE_VERSION_PATCH "${PROJECT_VERSION_PATCH}")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "AIDE")
set(CPACK_STRIP_FILES TRUE)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
    set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
endif()

if(WIN32)
    if(NOT DEFINED CPACK_GENERATOR)
        set(CPACK_GENERATOR "NSIS;ZIP")
    endif()
    set(CPACK_NSIS_DISPLAY_NAME "AIDE - AI Code Editor")
    set(CPACK_NSIS_PACKAGE_NAME "AIDE")
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
    set(CPACK_NSIS_MODIFY_PATH ON)
    set(CPACK_NSIS_MUI_ICON "${AIDE_GENERATED_ICO}")
    set(CPACK_NSIS_MUI_UNIICON "${AIDE_GENERATED_ICO}")
    set(CPACK_NSIS_INSTALLED_ICON_NAME "bin\\\\aide.exe")
    set(CPACK_PACKAGE_EXECUTABLES "aide" "AIDE")
elseif(APPLE)
    if(NOT DEFINED CPACK_GENERATOR)
        set(CPACK_GENERATOR "DragNDrop;TGZ")
    endif()
    set(CPACK_DMG_VOLUME_NAME "AIDE")
elseif(UNIX)
    if(NOT DEFINED CPACK_GENERATOR)
        set(CPACK_GENERATOR "DEB;TGZ")
    endif()
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "${CPACK_PACKAGE_CONTACT}")
    set(CPACK_DEBIAN_PACKAGE_SECTION "devel")
    set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
endif()

include(CPack)

