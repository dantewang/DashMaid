# DashMaid

DashMaid is a Windows desktop launcher. It shows a toolbar that stays above all other windows. Each item on the toolbar is an app shortcut or a widget. When the pointer moves over an item, the item expands with a spring animation. The two items next to it grow by smaller amounts.

## Status

The toolbar process works for app items: icons, the hover animation, click to start, the right-click menu, drag to move, and the tray icon. The run box works: the round button or the run hotkey opens it. It searches the toolbar apps and the Start menu apps (name, path, and pinyin initials), and it runs commands like the Windows Run dialog. Widgets work: clock, CPU, memory, network, and command output. The hotkey positions, AppBar registration, and the settings window do not exist yet. The 设置 menu command opens `config.json` in Notepad until the settings window exists.

An HTML prototype shows the interaction and the layout. The prototype is published as a private Artifact at https://claude.ai/artifact/HguynqyhfMH1omXntvjZMg. A local copy is at `prototype/dashmaid.html`. Git ignores the `prototype/` folder. Do not commit the prototype.

## Target platform

- Windows 11 only.
- The app is not a Microsoft Store app. Do not package it as MSIX for the Store. The app runs unpackaged.

## Tech stack

- Toolbar process (always running): C++20, Win32, and Windows.UI.Composition through C++/WinRT. Draw text and icons with Direct2D and DirectWrite into Composition drawing surfaces.
- Animation: the toolbar process calculates the springs on the CPU and writes the values to the Composition visuals once per compositor frame (`DCompositionWaitForCompositorClock`). Hit tests use the same values, so the item under the pointer is always the item that the user sees. Do not replace this with Composition spring animations.
- Settings window (separate process, starts on demand): C# on .NET 10 with WinUI 3. Use the Windows App SDK unpackaged (`WindowsPackageType` = `None`) and self-contained (`WindowsAppSDKSelfContained` = `true`). This deployment needs no MSIX package, no Store, and no separate runtime installer. The process exits when the window closes.
- Shared configuration: `%APPDATA%\DashMaid\config.json`. The toolbar process watches this file for changes.
- Resource use: balance code complexity, CPU and GPU use, and memory use. Memory has a soft target of 30 MB working set for the toolbar process. The target is not a hard requirement.
- Do not use memory optimizations that make the app slower or use more CPU or GPU time. Examples: `EmptyWorkingSet` or other working-set trims, releasing a resource and creating it again when it is necessary, decoding again instead of keeping a cache, and software rendering (WARP) instead of the GPU.
- Do not use Rust. Do not use web technology (Electron, WebView2 UI, Tauri, or similar).

Installed on the development machine:

- .NET SDK 10.0.401
- Visual Studio Build Tools 2026 (18.x) with the MSVC x64 tools
- CMake from Visual Studio Build Tools: `C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe` (not on `PATH`)
- Windows SDK 10.0.26100.0

## Repository layout

- `src/Toolbar/`: the toolbar process (`DashMaid.exe`).
  - `App.*`: window, message loop, input, menus, tray icon.
  - `Scene.*`: Composition visual tree, layout, springs, hit tests.
  - `Config.*`: read and write `config.json`.
  - `Icons.*`: icon extraction and main color.
  - `Launch.*`: start apps, open the file location, clipboard.
  - `AppCatalog.*`: list of the Start menu apps, built on a worker thread.
  - `Search.*`: ranking for the run box, and command-line parsing.
  - `Pinyin.*`: pinyin initials of Chinese characters.
  - `TextEdit.*`: single-line text editing for the run box.
  - `Hotkey.*`: parse hotkey text such as `Ctrl+Alt+R`.
  - `Widgets.*`: widget data (clock, CPU, memory, network) and the command runner.
  - `Metrics.h`: sizes in DIPs. `Strings.h`: UI text.
- `tests/`: unit tests for the code that has no UI (`ToolbarTests`). Run them with `ctest --test-dir build -C Debug`.
- `build/`: CMake output. Git ignores this folder.

## Build

Run these commands from the repository root:

```
"C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" -S . -B build -G "Visual Studio 18 2026" -A x64
"C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build build --config Debug
```

The output is `build\src\Toolbar\Debug\DashMaid.exe`. Only one instance runs at a time. To stop it, right-click the tray icon and select 退出.

## Configuration file

`%APPDATA%\DashMaid\config.json` (UTF-8). The toolbar writes the defaults when the file does not exist. Fields:

- `position`: `mode` (`float`, `dock`, `center`, `cursor`), `x` (window center, screen pixels), `y` (window top, screen pixels).
- `theme`: `dark` or `light`. `material`: `acrylic` or `solid`. `collapsedStyle`: `slice` or `glow`.
- `widgetDisplay`: `text` (widgets always show their text) or `collapse` (widgets collapse like app items).
- `spring`: `stiffness`, `damping`.
- `hotkeys`: `run` (default `Ctrl+Alt+R`). An empty string disables the hotkey.
- `items`: array of objects. The toolbar shows them in this order.
  - App: `type` (`app`), `name`, `path`, `args`, `workingDir`, `style` (`default`, `slice`, `glow`), `admin`.
  - Widget: `type` (`widget`), `source` (`clock`, `cpu`, `memory`, `network`, `command`), `click` (the file, URI, or program that a click opens. If `click` is empty, the toolbar uses the default of the source). A command widget also has `name`, `command` (runs through `cmd.exe /c`), `interval` (seconds), and optional `color` (`#RRGGBB`, text color in the dark theme).
- If the file exists but is not valid, the toolbar keeps the last valid settings and does not write the file.

## Required behavior

- Toolbar height: 48 px at 100% display scale. This value is the default Windows 11 taskbar height. Use a fixed value. Do not read the height from the system.
- Toolbar positions:
  - Floating: topmost in Z-order. The user can drag the toolbar to a new position.
  - Screen top: topmost and attached to the top edge. Optional AppBar registration keeps maximized windows below the toolbar.
  - Hidden until a global hotkey shows it at the center of the screen.
  - Hidden until a global hotkey shows it next to the pointer.
- App items: the user selects an app from the Start menu lists ("Pinned" and "All apps") or selects an executable file and arguments manually.
  - Collapsed style 1: a part of the app icon.
  - Collapsed style 2: the main color of the icon with a glow.
  - Expanded: a rounded square that shows the full icon.
  - Click starts the app. Right-click opens a menu. The menu must contain "Run as administrator".
- Run box: the round button at the left end of the toolbar opens a text box. A global hotkey also opens it. It works like the Windows Run dialog.
- Widgets: items that show live data (clock, CPU, memory, network, and output of a user command). The user selects if widgets collapse like app items or always show their text.

## UI text rules

These rules apply to text that the app shows to users. They do not apply to code identifiers.

- Do not use the words "pill", "segment", or "条" in the UI. Use neutral words that desktop software uses.
- Do not add text that explains how a feature works. Labels name the setting or the action only.
- Do not show or explain sizes to the user.

Chinese UI terms:

| Concept | UI term |
|---|---|
| The whole bar | 工具栏 |
| An entry on the bar | 项目 |
| App entry | 应用 |
| Live data entry | 小组件 |
| Collapsed style: part of the icon | 图标局部 |
| Collapsed style: main color with glow | 主题色光晕 |
| Run box | 运行 |

## Design credit

The toolbar interaction comes from the Framer component "Segmented Navigation" by Till Janek. The About page must show only these items:

- Component: https://www.framer.com/marketplace/components/segmented-navigation/
- Author: Till Janek
- X: https://x.com/tilljanek (@tilljanek)

Do not copy code from the original component. Reimplement the interaction.

## Writing rules

- Replies to the user in a session: write in Chinese unless the user asks for a different language.
- English text in replies and in repository documents: follow ASD-STE100 Simplified Technical English (https://github.com/danyuchn/asd-ste100-skill). Use the STE-flavored mode for prose and the strict mode for procedures.
- Chinese text in replies and in repository documents: follow https://github.com/Fenng/Tech-Doc-Style-Chinese.
- UI text in the app does not need to follow these two guides. It must follow the UI text rules above.

## Git rules

- Author and committer: `Dante Wang <coolcfan@gmail.com>`. The repository sets this identity in its local Git configuration.
- Do not add trailers to commit messages or pull request descriptions. This includes `Co-Authored-By:`, `Claude-Session:`, "Generated with" footers, and session links.
- Write commit messages in English and follow ASD-STE100.
