# CMakeLists.txt
cmake_minimum_required(VERSION 3.20)
project(AIDE VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network)

add_executable(aide
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
)

target_include_directories(aide PRIVATE src)
target_link_libraries(aide PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network)