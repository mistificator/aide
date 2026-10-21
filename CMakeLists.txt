# CMakeLists.txt
cmake_minimum_required(VERSION 3.20)
project(AIDE
    VERSION 1.0.0
    DESCRIPTION "AI Development Environment - Multi-Document Qt6 Code Editor"
    HOMEPAGE_URL "https://github.com/your-org/aide"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network)

# Обход удаления AGL.framework в свежих macOS SDK (QTBUG-137687)
if(APPLE AND TARGET WrapOpenGL::WrapOpenGL)
    get_target_property(_gl_libs WrapOpenGL::WrapOpenGL INTERFACE_LINK_LIBRARIES)
    if(_gl_libs)
        list(FILTER _gl_libs EXCLUDE REGEX "AGL")
        set_target_properties(WrapOpenGL::WrapOpenGL PROPERTIES INTERFACE_LINK_LIBRARIES "${_gl_libs}")
    endif()
endif()

include(GNUInstallDirs)

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
    src/ui/MainWindow.h
    src/ui/MainWindow.cpp
    .github/workflows/ci.yml
)

target_include_directories(aide PRIVATE src)
target_link_libraries(aide PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network)

# Метаданные для macOS App Bundle
set_target_properties(aide PROPERTIES
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.aide.editor"
    MACOSX_BUNDLE_BUNDLE_NAME "AIDE"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}"
)

# Правила установки бинарника
install(TARGETS aide
    BUNDLE  DESTINATION . COMPONENT Runtime
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT Runtime
)

# Развёртывание рантайма Qt (windeployqt / macdeployqt)
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
set(CPACK_PACKAGE_CONTACT "maintainer@aide.org")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
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
    set(CPACK_PACKAGE_EXECUTABLES "aide" "AIDE")
    set(CPACK_NSIS_MENU_LINKS "https://github.com/your-org/aide" "AIDE Homepage")
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
