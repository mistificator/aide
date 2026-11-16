# AIDE — AI Development Environment

**AIDE** is a cross-platform, multi-document code editor built with **Qt6** and modern **C++20**, engineered for deep, transparent integration with local and remote Large Language Models (LLMs).

It combines a fast tabbed code editor with non-blocking token streaming, atomic single-step undo (`Ctrl+Z`), hierarchical bottom-up project and subproject summarization (`.aide`), a managed local LLM server runner, a connection-aware two-tier priority task queue, and an interactive Markdown & code-extraction workbench.

---

## Key Features

### 1. Editor & Streaming AI Code Transformations
* **Multi-Document Tabbed Interface**: Built on `QTabWidget` and `QPlainTextEdit` with line numbering, active line highlighting, syntax highlighting, and tab state badges for unsaved edits (`*`) and active AI tasks (`[AI]`).
* **Per-Document Non-Blocking AI Lock**: Triggering an AI operation locks only the target editor tab into read-only mode with an overlay banner and a **Cancel** button, leaving all other open documents completely responsive.
* **Real-Time Streaming with Atomic Undo (`Ctrl+Z`)**: Uses `QTextCursor::joinPreviousEditBlock()` to stream incoming tokens directly into the document without resetting the undo stack or freezing syntax highlighting. Reverting an entire AI edit takes a single `Ctrl+Z`.
* **Smart Stream Auto-Scroll**: Automatically follows the cursor during code generation, but pauses auto-scrolling if you scroll up to inspect earlier lines, resuming when you scroll back to the bottom.
* **Instant Cancellation & Rollback**: Clicking **Cancel** aborts the active network stream and immediately rolls back the document to its pre-generation state.
* **Multi-Turn Continuation & Follow-Ups**: Continue cut-off generations seamlessly from the exact stopping point (`Continue Last AI Generation`) or send follow-up instructions (`Continue Last AI Request (Follow-up)...`) while preserving full conversation history.
* **Two-Pane Side-by-Side Diff Viewer (`Ctrl+D`)**: Built-in LCS diff viewer with synchronized scrollbars and syntax highlighting to compare unsaved editor changes against disk or inspect AI-generated code replacements before applying them.

### 2. Connection Profiles, Local LLM Runner & Status Lamp
* **Multi-Backend LLM Support**:
  * **Ollama** (Native `/api/generate`, `/api/chat`, and `/api/tags` with NDJSON streaming).
  * **KoboldCpp** (Native `/api/extra/generate/stream` SSE, `/api/v1/generate`, and `/api/v1/model`).
  * **OpenAI-Compatible** (`/v1/chat/completions` and `/v1/models` SSE for `llama-server`, LM Studio, vLLM, and cloud providers).
* **Reasoning / Thinking Models Support**: Automatically detects and separates reasoning blocks (`<think>`, `<thinking>`, `<thought>`, `<|begin_of_thought|>`, and `reasoning_content` deltas) from final answers, displaying live thinking token counts and generation speed.
* **Quick Profile Switchers**: Switch between configured AI Connection Profiles directly from both the **AI Toolbar** and the **Status Bar**.
* **Local LLM Executable Process Manager**:
  * Configure a local LLM server binary (e.g., `llama-server`, `koboldcpp`) and command-line arguments per profile.
  * **Browse Model...** button with smart path substitution into CLI arguments (replaces `{model}` placeholders or updates existing `-m` / `--model` flags automatically).
  * **Start**, **Stop**, and **Restart (`Restart (Apply Changes)`)** controls with an animated indeterminate progress bar and OS-level process detection (`Toolhelp32Snapshot` on Windows, `/proc` on Linux).
  * **Auto-Start & Profile Sync**: Optional shared setting to automatically launch the configured local LLM binary on AIDE startup and automatically stop the previous process / start the new one when switching profiles.
  * **Live Process Logging**: Redirects `stdout` and `stderr` from the local LLM process directly into the **AI API Debug Console** (`[LLM_STDOUT]`, `[LLM_STDERR]`, `[LOCAL_LLM]`).
* **Status Bar Connection Indicator Lamp**:
  * **Gray Lamp (`LLM: Disconnected`)**: No verified connection. `AiTaskQueue` is paused so queued tasks never fail or spin in vain.
  * **Yellow Lamp (`LLM: Connecting...` / `LLM: Waiting for local port...`)**: Active `Test / Fetch` probe in progress, or automatic background polling (up to 2 minutes) waiting for a newly launched local LLM process to load model weights and open its HTTP port.
  * **Green Lamp (`LLM: Connected`)**: `Test / Fetch` succeeded. `AiTaskQueue` automatically unpauses and begins processing tasks.

### 3. Hierarchical Project Context & Subprojects (`.aide`)
* **Nested "Matryoshka" Subprojects**:
  * Supports both **Standalone `.aide` Subprojects** (stored in `<subfolder>/<subfolder>.aide`) and **Virtual Subprojects** (stored inside the parent `.aide` file).
  * Convert any folder between a Virtual Subproject and a Standalone `.aide` file via the Project Explorer context menu without touching source files on disk; existing file summaries migrate automatically.
* **Bottom-Up Summary Synthesis**:
  * File summaries are indexed using `SHA-256` content hashes.
  * Subproject architectural summaries are synthesized from their direct files and child subproject summaries.
  * The root project summary is synthesized bottom-up from all nested scopes.
* **Live File System Reconciliation**: Tracks file renames and deletions via `QFileSystemWatcher`, automatically migrating summaries on rename and removing stale summaries on file deletion.
* **Recursive Summary Generator**: Interactive file-tree dialog (`AI -> Project -> Generate Summaries (Recursive)...`) with presets for missing summaries or full regeneration, respecting allowed extensions, exact filenames (`CMakeLists.txt`, `Makefile`, `Dockerfile`), and ignored directory wildcards.
* **Export Project Summaries as Wiki (Markdown)**: Exports all root, subproject, and file AI summaries into a navigable directory tree of Markdown files (`README.md` per scope with breadcrumbs and links to individual `<filename>.md` pages).

### 4. Interactive Markdown & Extended Code View
* **Built-In Markdown & LaTeX Renderer**: Renders headings, tables, blockquotes, lists, fenced code blocks, and inline/display LaTeX mathematical expressions (`$...$`, `$$...$$`, `\[...\]`, Greek symbols, fractions, superscripts/subscripts, matrices, and operators).
* **Three Display Modes** (available for any `.md` editor tab via `Ctrl+Shift+M` and in the **Project Custom Prompt** result window):
  1. **View Raw Markdown**: Editable raw Markdown source with syntax highlighting.
  2. **View Rendered Markdown**: Clean, formatted read-only document view.
  3. **Extended Markdown Code View**: Split-pane workbench that extracts all fenced code blocks, automatically detects target file paths from headings or leading comments, matches them against existing project files (`[Existing]` vs `[New]`), and provides one-click actions:
     * **Open & Replace in Editor (Unsaved)** (reversible via `Ctrl+Z`)
     * **View Diff...** (two-pane comparison against the existing project file on disk)
     * **Open as New Tab**
     * **Save File / Fragment As...** or **Save All Detected Files...**

### 5. Docks, Token Guard & Dynamic KDE Syntax Highlighting
* **AI Context Inspector Dock**: Inspect and edit the active **Document** summary and instructions, switch between the root project and any **Subproject** scope (with in-place Markdown editing and `Continue` generation), and preview the assembled **Effective Context** with character and token estimates.
* **AI Prompt History Dock**: Stores custom prompts deduplicated by `SHA-256` hash. Supports instant search, pinning to top, reordering, editing, and one-click reuse in Document or Project custom prompts.
* **AI Task Queue Dock**: Real-time monitor for the two-tier FIFO scheduler (`High Priority` user actions vs `Low Priority` background summaries). Displays batch progress, elapsed time, TTFT, generation throughput (`tok/s`), `Thinking` status, and per-task cancellation.
* **AI API Debug Console Dock**: Logs raw HTTP requests/responses, optional SSE/NDJSON stream chunks, completion statistics, and local LLM process `stdout`/`stderr`.
* **Context Window & Token Limit Check**: Automatically estimates prompt + output token requirements before execution and offers an interactive dialog to raise `Context Window` and `Max Output Tokens` (with optional saving to the active profile) if limits are exceeded.
* **Dynamic KDE Kate XML Syntax Highlighting**: Ships with built-in support for C, C++, Python, Bash, SQL, and JavaScript, plus an indexer in `Edit -> File Types & Syntax Preferences...` that fetches and installs any of the 300+ syntax XML definitions from the official KDE `syntax-highlighting` repository or imports local `.xml` files.
* **Localization**: Configurable output language in `AI -> Preferences -> Behavior & Summary Rules` instructing the LLM to write Markdown explanations, summaries, and code comments in your preferred language.

---

## Architecture Overview

```text
                       ┌────────────────────────────────────────────┐
                       │                 MainWindow                 │
                       │  (Menus, AI Toolbar, Status Bar Lamp &     │
                       │   Quick Profile Switchers, Token Guard)    │
                       └──────┬───────────────┬──────────────┬──────┘
                              │               │              │
        ┌─────────────────────┴──┐     ┌──────┴───────┐   ┌──┴────────────────────────┐
        │    Project Explorer    │     │  EditorTabs  │   │ Docks:                    │
        │  (ProjectTreeModel +   │     │ (CodeEditor +│   │ - ContextInspectorDock    │
        │   Incremental Search)  │     │  3-Mode MD)  │   │ - PromptHistoryDock       │
        └───────────┬────────────┘     └──────┬───────┘   │ - AiTaskQueueDock         │
                    │                         │           │ - AiDebugConsoleDock      │
                    ▼                         ▼           └────────────┬──────────────┘
        ┌──────────────────────────────────────────────────────────────┴──────────────┐
        │                               ProjectManager                                │
        │       (Root & Nested .aide Files, Virtual Subprojects, SHA-256 Hashes,      │
        │        FileSystemWatcher Reconciliation, Markdown Wiki Exporter)            │
        └─────────────────────────────────────┬───────────────────────────────────────┘
                                              │
                         ┌────────────────────┴────────────────────┐
                         ▼                                         ▼
              ┌─────────────────────┐                   ┌─────────────────────┐
              │    SummaryWorker    │                   │     AiTaskQueue     │
              │ (Bottom-Up Scope    │──────────────────►│ (High: User FIFO    │
              │  Synthesis & Batch) │                   │  Low: Bg FIFO,      │
              └─────────────────────┘                   │  Gated by Green     │
                                                        │  Connection Lamp)   │
                                                        └──────────┬──────────┘
                                                                   │
                         ┌─────────────────────────────────────────┴──────────┐
                         ▼                                                    ▼
              ┌─────────────────────┐                              ┌─────────────────────┐
              │     AiProvider      │                              │LocalLlmProcessMgr   │
              │ (Ollama / KoboldCpp │                              │ (CLI Runner, PID    │
              │  / OpenAI Streaming │                              │  Check, Auto-Start, │
              │  & Reasoning Parser)│                              │  stdout/stderr Log) │
              └─────────────────────┘                              └─────────────────────┘
```

### Module Breakdown

| Directory | Primary Classes | Responsibilities |
| :--- | :--- | :--- |
| `src/core/` | `ConfigManager`, `ProjectManager` | Manages `~/.aide/config.json` and `prompts.json`, connection profiles, prompt history, root and nested `.aide` files, virtual subprojects, `SHA-256` verification, and Wiki export. |
| `src/editor/` | `CodeEditor`, `EditorTabWidget` | Line number gutter, per-tab AI lock banner, streaming `joinPreviousEditBlock()` edits, Raw/Rendered/Extended Markdown tab modes, and OS Explorer/Terminal launchers. |
| `src/ai/` | `AiProvider`, `AiTaskQueue`, `SummaryWorker` | Asynchronous NDJSON/SSE streaming, `<think>` tag separation, connection-gated two-tier FIFO queue, and bottom-up file/subproject/project summary scheduling. |
| `src/syntax/` | `LexicalHighlighter`, `ColorScheme`, `ThemeAndSyntaxDownloader` | KDE Kate XML syntax parsing, MIME/extension mapping, Dracula/Monokai/GitHub-Light themes, and Windows dark title bar / Fusion proxy styling. |
| `src/ui/` | `MainWindow`, `ContextInspectorDock`, `AiPreferencesDialog`, `LocalLlmProcessManager`, `ProjectPromptResultDialog`, `DiffViewerDialog`, `RecursiveSummaryDialog`, `PromptHistoryDock`, `AiTaskQueueDock`, `AiDebugConsoleDock`, `EditorPreferencesDialog`, `MarkdownRenderer`, `AppIcon` | Full GUI layout, local LLM process lifecycle, Markdown/LaTeX rendering, code block extraction, two-pane diffing, and preference dialogs. |
| `src/tools/` | `IconGenerator` | Build-time utility that procedurally renders `aide.ico`, `aide.svg`, and action SVG icons. |

---

## Prerequisites & Dependencies

* **C++ Compiler**: C++20 compatible (`GCC 11+`, `Clang 13+`, or `MSVC 2022`).
* **Build System**: `CMake 3.20+` and `Ninja` (recommended) or `Make`.
* **Qt Framework**: `Qt 6.2+` (`Qt6::Core`, `Qt6::Gui`, `Qt6::Widgets`, `Qt6::Network`).

### Installing Dependencies

#### Ubuntu / Debian
```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake ninja-build pkg-config \
  libxkbcommon-dev libgl1-mesa-dev qt6-base-dev
```

#### Fedora / RHEL
```bash
sudo dnf install -y gcc-c++ cmake ninja-build qt6-qtbase-devel
```

#### macOS (Homebrew)
```bash
brew install cmake ninja qt@6
```

#### Windows
Install **Visual Studio 2022** (Desktop development with C++) and **Qt 6.x for MSVC 2022 64-bit**.

---

## Build, Run & Packaging

### Compile from Source
```bash
# Clone the repository
git clone https://github.com/your-org/aide.git
cd aide

# Configure build (Release)
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release

# Compile binary and procedural icons
cmake --build build --config Release --parallel

# Launch AIDE
./build/aide
```

### Generate Installers with CPack
AIDE includes `CPack` rules for generating platform-specific installers and portable archives (`.deb` and `.tar.gz` on Linux, `NSIS .exe` and `.zip` on Windows, `.dmg` and `.tar.gz` on macOS):

```bash
cd build
cpack -C Release -B packages
```

---

## Configuration & Local LLM Setup

Open **AI -> Preferences...** to configure connection profiles and local model execution:

1. **Connection Profiles**:
   * Create presets for **Ollama (Native API)** (`http://127.0.0.1:11434`), **KoboldCpp (Native / SSE)** (`http://localhost:5001`), or **OpenAI-Compatible** (`http://localhost:5001/v1/`).
   * Switch active profiles at any time from the **AI Toolbar** or **Status Bar**.
2. **Local LLM Executable & Command Line**:
   * Enable **Use / Launch Local LLM Executable for This Profile**.
   * Click **Browse...** to select your local server binary (e.g., `llama-server.exe` or `koboldcpp.exe`).
   * Click **Browse Model...** to pick a `.gguf` / `.safetensors` / `.kobold` file from disk; AIDE automatically inserts or updates the `-m` / `--model` argument (or replaces `{model}` in your custom argument string).
   * Use **Start**, **Stop**, and **Restart (Apply Changes)** to control the process directly, or enable **Auto-start local executable on AIDE launch and switch process when changing profiles**.
3. **Connection Indicator Lamp**:
   * On startup, profile switch, or local LLM launch, AIDE automatically runs **Test / Fetch Models**.
   * While a local model is loading into memory, the status bar lamp stays **Yellow (`LLM: Waiting for local port...`)** and polls the endpoint every 2 seconds. Once the HTTP server responds, the lamp turns **Green (`LLM: Connected`)** and the AI task queue begins processing.

---

## Storage Structure

### Global User Configuration (`~/.aide/`)
```text
~/.aide/
├── config.json          # Profiles, local executable paths/CLI args, behavior rules, prompt history, UI state
├── prompts.json         # Customizable prompt templates for all editor and project AI actions
├── syntax/              # Downloaded KDE Kate XML syntax definitions (cpp.xml, python.xml, rust.xml, etc.)
└── themes/              # Color scheme JSON files (dracula.json, monokai.json, github-light.json)
```

### Project Context File (`<project_name>.aide`)
Stored in the root of an opened project folder (and optionally in subfolders marked as standalone subprojects):
```json
{
  "project_name": "my_project",
  "project_system_prompt": "Use C++20 and Qt6 conventions.",
  "project_summary": "Synthesized architectural overview...",
  "project_summary_llm": "ollama:qwen2.5-coder:7b",
  "project_summary_dirty": false,
  "updated_at": "2026-11-14T21:00:00Z",
  "virtual_subprojects": {
    "src/core": {
      "project_name": "core",
      "project_system_prompt": "",
      "project_summary": "Core configuration and project state management...",
      "project_summary_llm": "ollama:qwen2.5-coder:7b",
      "project_summary_dirty": false,
      "updated_at": "2026-11-14T20:58:00Z"
    }
  },
  "files": {
    "src/main.cpp": {
      "sha256": "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
      "llm_model": "ollama:qwen2.5-coder:7b",
      "user_prompt": "",
      "ai_summary": "Application entry point initializing QApplication, theme filter, and MainWindow.",
      "summary_created": true,
      "updated_at": "2026-11-14T20:55:00Z"
    }
  }
}
```

---

## Keyboard Shortcuts

| Shortcut | Action |
| :--- | :--- |
| `Ctrl + N` | New Document |
| `Ctrl + O` | Open File... |
| `Ctrl + Shift + O` | Open Folder / Project... |
| `Ctrl + Shift + W` | Close All Projects |
| `Ctrl + S` | Save Current Document (schedules background summary if enabled) |
| `Ctrl + Shift + S` | Save All Modified Documents |
| `Ctrl + D` | Compare Unsaved Changes (Two-Pane Diff Viewer) |
| `Ctrl + Shift + P` | AI Custom Prompt for Project... |
| `Ctrl + Shift + M` | Toggle Markdown View Mode (Raw / Rendered / Extended Code View) |
| `Ctrl + W` | Close Active Document |
| `Ctrl + Z` / `Ctrl + Y` | Undo (reverts full streaming AI edit in one step) / Redo |
| `Ctrl + F` | Find & Replace Dialog |
| `Ctrl + Tab` / `Ctrl + Shift + Tab` | Next / Previous Document Tab |
| `Ctrl + +` / `Ctrl + -` | Zoom Editor Font In / Out |
| `Ctrl + Q` | Exit Application |

---

## License

This project is licensed under the MIT License. Syntax highlighting XML definitions downloaded from the KDE `syntax-highlighting` repository into `~/.aide/syntax/` are distributed under their respective upstream licenses (MIT / LGPL).