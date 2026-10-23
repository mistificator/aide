# AIDE — AI Development Environment

AIDE is a cross-platform, multi-document code editor built with Qt6 and modern C++20, engineered for deep integration with local and remote Large Language Models (LLMs). It features non-blocking token streaming with single-step undo recovery, automatic background hierarchical project context summarization, and a two-tier priority execution queue.

---
<img width="2099" height="1332" alt="image" src="https://github.com/user-attachments/assets/25d5cac2-f885-4ef9-b7cc-a7f1cb7637c9" />
---

## Key Features

* **Multi-Document Tabbed Interface**: Built on `QTabWidget` and `QPlainTextEdit`, supporting line numbering, active line highlighting, and visual indicators for unsaved modifications (`*`) and active AI generation (`[AI]`).


* **Per-Document Non-Blocking AI Lock**: Running an AI operation locks only the target document into a read-only state with a status banner and a **Cancel** button, leaving all other open tabs fully interactive.


* **Real-Time Streaming with Atomic Undo (`Ctrl+Z`)**: Uses `QTextCursor::joinPreviousEditBlock()` to stream response tokens dynamically without freezing UI repaints or syntax rehighlighting, while preserving the ability to revert the entire AI edit in a single `Ctrl+Z` step.


* **Instant Cancellation**: Clicking **Cancel** on an active document aborts the underlying network request and immediately rolls back changes to the pre-generation state.


* **Automatic Code Fence Stripping**: Intercepts and strips wrapping Markdown backticks (`````) on the fly during code-replacement operations.


* **Two-Tier Priority AI Scheduler**: Dispatches user-initiated code transformations via a high-priority FIFO queue, while background file and project summarizations run through a low-priority FIFO queue with automatic task deduplication.


* **Hierarchical Context Synthesis (`.aide`)**: Maintains file-level summaries and a global architectural project summary stored in a portable `<project_name>.aide` JSON file.


* **Background SHA-256 Indexing**: Tracks file content hashes and schedules background summaries whenever documents are opened or saved (`Ctrl+S`).


* **Multi-Backend LLM Support**: Connects natively to Ollama (NDJSON streaming), KoboldCpp (SSE streaming), and OpenAI-compatible endpoints.


* **Lexical Syntax Highlighting**: Supports C, C++, Python, Bash, SQL, and JavaScript with automated download and caching of KDE syntax definitions and JSON themes.


* **AI Context Inspector**: A dedicated right-hand dock displaying project prompts, document-specific instructions, AI summaries, and a live preview of the assembled LLM prompt with token estimation.



---

## Architecture Overview

AIDE relies on Qt's asynchronous signal-and-slot event loop (`QEventLoop`) to run non-blocking network calls, editor updates, and background workers inside the main thread without GUI freezes.

```text
                     ┌────────────────────────────────────────┐
                     │               MainWindow               │
                     └────┬──────────────┬──────────────┬─────┘
                          │              │              │
       ┌──────────────────┴───┐   ┌──────┴─────┐   ┌────┴─────────────────┐
       │   Project Explorer   │   │ EditorTabs │   │ ContextInspectorDock │
       │   (ProjectTreeModel) │   │(CodeEditor)│   │   (System Prompts)   │
       └──────────┬───────────┘   └──────┬─────┘   └──────────┬───────────┘
                  │                      │                    │
                  ▼                      ▼                    ▼
       ┌──────────────────────────────────────────────────────────────┐
       │                        ProjectManager                        │
       │     (<project_name>.aide, SHA-256 Hashes, File/Proj Context) │
       └──────────────────────────────┬───────────────────────────────┘
                                      │
                   ┌──────────────────┴──────────────────┐
                   ▼                                     ▼
        ┌─────────────────────┐               ┌─────────────────────┐
        │    SummaryWorker    │               │     AiTaskQueue     │
        │(Periodic Rebuild /  │               │ (High: User Actions │
        │ File Save Triggers) │               │  Low: Background)   │
        └──────────┬──────────┘               └──────────┬──────────┘
                   │                                     │
                   └──────────────────┬──────────────────┘
                                      ▼
                           ┌─────────────────────┐
                           │     AiProvider      │
                           │  (Ollama/KoboldCpp/ │
                           │  OpenAI-Compatible) │
                           └─────────────────────┘

```

### Module Breakdown

| Module | Primary Classes | Responsibilities |
| --- | --- | --- |
| `src/core/` | `ConfigManager`, `ProjectManager` | Manages `~/.aide/config.json`, prompt templates, `.aide` project serialization, and SHA-256 verification.|
| `src/editor/` | `CodeEditor`, `EditorTabWidget` | Implements line numbers, per-tab locking banners, streaming `joinPreviousEditBlock()`, and tab lifecycle.|
| `src/ai/` | `AiProvider`, `AiTaskQueue`, `SummaryWorker` | Network streaming (NDJSON/SSE), two-tier priority queue scheduling, and background context compilation.|
| `src/syntax/` | `LexicalHighlighter`, `ColorScheme`, `ThemeAndSyntaxDownloader` | Syntax highlighting for 6 languages, theme parsing, and automatic asset downloads from KDE repositories.|
| `src/ui/` | `MainWindow`, `ContextInspectorDock`, `AiPreferencesDialog` | Menu system, project tree, context inspector tabs, and LLM configuration dialogs.|

---

## AI Operations & Two-Tier Scheduling

### Editor Actions

Right-clicking inside any open file opens the **AI ▸** context menu, which applies operations to either the selected block of code or the entire file:

* **Explain Code**: Generates an architectural explanation displayed in a separate non-destructive dialog.


* **Refactor & Clean Up**: Refactors the code for clarity, structure, and idiomatic style.


* **Find Bugs & Edge Cases**: Identifies security vulnerabilities, logic errors, and memory leaks with in-place corrections.


* **Optimize Performance**: Optimizes algorithmic time and space complexity.


* **Add Documentation / Comments**: Generates docstrings and Doxygen documentation.


* **Generate Unit Tests**: Produces test cases covering main flows and edge cases.


* **Complete / Continue Code**: Autocompletes code from function signatures or comments.


* **Custom Prompt...**: Opens an input dialog for user-defined instructions.



### Queue Priority Rules

| Queue | Tasks Handled | Scheduling Rule |
| --- | --- | --- |
| **High Priority (FIFO)** | User actions from editor and AI menus.| Executes immediately when the provider is free. Consecutive user requests across multiple tabs queue and run in arrival order.|
| **Low Priority (FIFO)** | File summaries and project summaries.| Executes only when the high-priority queue is empty. Deduplicates pending file summary requests for the same target path.|

---

## Project & Storage Structure

### Project File (`<project_name>.aide`)

Opening a project folder creates or loads a `<project_name>.aide` JSON file in the project directory:

```json
{
  "project_name": "my_project",
  "ignored_dirs": [".git", "build", "__pycache__", "node_modules", ".aide"],
  "project_system_prompt": "Use C++20, Qt6 style conventions, and smart pointers.",
  "project_summary": "Architectural digest synthesized by the LLM...",
  "project_summary_dirty": false,
  "updated_at": "2026-10-21T18:00:00Z",
  "files": {
    "src/main.cpp": {
      "sha256": "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
      "user_prompt": "Specific instructions for this file...",
      "ai_summary": "Entry point initializing QApplication and MainWindow...",
      "updated_at": "2026-10-21T18:05:00Z"
    }
  }
}

```

### Global User Storage (`~/.aide/`)

Application configurations and downloaded syntax definitions are stored in the user's home folder:

```text
~/.aide/
├── config.json          # Selected LLM provider, URLs, generation hyperparameters, and theme
├── prompts.json         # Prompt templates for refactoring, testing, and summarization
├── syntax/              # Downloaded syntax XML/JSON rules (cpp.xml, python.xml, etc.)
└── themes/              # Color schemes (dracula.json, monokai.json, github-light.json)

```

---

## Prerequisites & Dependencies

* **C++ Compiler**: Supporting C++20 (GCC 11+, Clang 13+, or MSVC 2019+).


* **Build System**: CMake 3.20 or newer.


* **Framework**: Qt 6.2+ with the following components:


* `Qt6::Core`

* `Qt6::Gui`

* `Qt6::Widgets`

* `Qt6::Network`




### Installing Dependencies

#### Ubuntu / Debian

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake qt6-base-dev libqt6network6

```

#### Fedora / RHEL

```bash
sudo dnf install -y gcc-c++ cmake qt6-qtbase-devel

```

#### macOS (Homebrew)

```bash
brew install cmake qt@6

```

---

## Build & Installation

```bash
# Clone the repository
git clone [https://github.com/your-org/aide.git](https://github.com/your-org/aide.git)
cd aide

# Generate build configuration
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Compile the application
cmake --build build -j$(nproc)

# Run AIDE
./build/aide

```

---

## Configuration & LLM Backend Setup

Navigate to **AI ▸ Preferences...** in the main menu to configure model connectivity:

* **Ollama**:
  * Set backend to `Ollama (Native API)`.
  * Endpoint URL: `http://127.0.0.1:11434`.
  * Click **Test / Fetch Models** to populate installed models (e.g., `qwen2.5-coder:7b`).
* **KoboldCpp**:
  * Set backend to `KoboldCpp (Native / SSE)`.
  * Endpoint URL: `http://127.0.0.1:5001`.
  * **OpenAI-Compatible**:
* Set backend to `OpenAI-Compatible`.
  * Endpoint URL: `http://127.0.0.1:8080` (or your proxy / vLLM / LM Studio address).
  * Enter your optional Bearer API key.

On first launch, AIDE creates `~/.aide/` and downloads bundled themes and syntax definitions automatically via background network calls.

---

## Keyboard Shortcuts

| Shortcut | Action |
| --- | --- |
| `Ctrl + N` | New file.|
| `Ctrl + O` | Open file.|
| `Ctrl + Shift + O` | Open folder / project.|
| `Ctrl + S` | Save current file (triggers background summary update).|
| `Ctrl + Shift + S` | Save all modified files.|
| `Ctrl + W` | Close active document.|
| `Ctrl + Z` | Undo last edit (reverts full AI generation if applied).|
| `Ctrl + Y` | Redo last edit.|
| `Ctrl + F` | Open Find & Replace dialog.|
| `Ctrl + Tab` | Switch to next open document.|
| `Ctrl + Shift + Tab` | Switch to previous open document.|
| `Ctrl + +` / `Ctrl + -` | Zoom editor font in / out.|

---

## License

This project is licensed under the MIT License. KDE syntax definition files downloaded into `~/.aide/syntax/` are distributed under their respective licenses from the KDE KSyntaxHighlighting project.
