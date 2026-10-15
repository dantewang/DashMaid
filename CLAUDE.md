# DashMaid

DashMaid is a Windows desktop launcher. It shows a toolbar that stays above all other windows. Each item on the toolbar is an app shortcut or a widget. When the pointer moves over an item, the item expands with a spring animation. The two items next to it grow by smaller amounts.

## Status

The toolbar process works for app items: icons, the hover animation, click to start, the right-click menu, drag to move, and the tray icon. The run box works: the round button or the run hotkey opens it. It searches the toolbar apps and the Start menu apps (name, path, and pinyin initials), and it runs commands like the Windows Run dialog. Widgets work: clock, CPU, memory, network, and command output. The center and cursor modes work: the toggle hotkey shows the toolbar, and Esc, the hotkey, a click outside, or a started app hides it. The screen top mode works, with an optional AppBar. The toolbar hides while a full-screen app runs on its monitor, and it can start with Windows. The settings window works: the 设置 menu command opens it. Changes go to `config.json` at once, and the toolbar applies them.

The look of the toolbar must be the same as the prototype: colors, shadows, antialiasing, and timing. `Scene.cpp` uses the CSS values of the prototype directly. Exceptions: the sizes come from the original component (the prototype uses a 48 px toolbar, so the effects of the items are multiplied by 1.5), the run button is flat, and app items in the "图标局部" style also have a glow. To compare, put the app and the prototype on the same plain background and compare the pixels.

The toolbar uses one window. The window has margins for the shadows. At rest, the window region is a rectangle around the toolbar and its visible shadow, so the rounded corners keep their antialiasing. While the toolbar is open or moves, the region is the full window. The compositor draws the shadows (`DropShadow`) and the app items, so an animation frame only writes sizes, offsets, and colors.

The background is opaque by default, like the original component. Its shadow is the shadow of the original component.

An HTML prototype shows the interaction and the layout. The prototype is published as a private Artifact at https://claude.ai/artifact/HguynqyhfMH1omXntvjZMg. To read it, use the Artifact read action. Git ignores the `prototype/` folder. Do not commit the prototype.

## Target platform

- Windows 11 only.
- The app is not a Microsoft Store app. Do not package it as MSIX for the Store. The app runs unpackaged.

## Tech stack

- Toolbar process (always running): C++20, Win32, and Windows.UI.Composition through C++/WinRT. Draw text and icons with Direct2D and DirectWrite into Composition drawing surfaces.
- Animation: the toolbar process calculates the springs on the CPU and writes the values to the Composition visuals once per compositor frame (`DCompositionWaitForCompositorClock`). Hit tests use the same values, so the item under the pointer is always the item that the user sees. Do not replace this with Composition spring animations.
- Settings window (separate process, starts on demand): C# on .NET 10 with WinUI 3. Use the Windows App SDK unpackaged (`WindowsPackageType` = `None`) and self-contained (`WindowsAppSDKSelfContained` = `true`), compiled with Native AOT for the release. This deployment needs no MSIX package, no Store, and no separate runtime installer. The process exits when the window closes.
- Shared configuration: `config.json`, next to `DashMaid.exe` if the file exists there, else in `%APPDATA%\DashMaid`. Both processes use this rule. The toolbar process watches both folders for changes.
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
  - `Monitors.*`: find the monitor of the screen top mode by its device path.
  - `Paint.*`: CSS rules in Direct2D: box shadows, shadow-list interpolation, cubic-bezier timing, and transitions.
  - `Effects.*`: Direct2D effect descriptions for Composition effect brushes (blur, saturate), without Win2D.
  - `Metrics.h`: sizes in DIPs. `Strings.h`: UI text.
  - `DashMaid.rc`: the application icon (resource ID 1). The window class and the tray icon use it.
- `src/Settings/`: the settings window (`DashMaid.Settings.exe`, C# and WinUI 3).
  - `ConfigStore.cs`: read and write `config.json`. Fields that the window does not know stay in the file.
  - `Pages.cs`: the pages (常规, 项目, 小组件, 外观, 快捷键, 关于). `Ui.cs`: layout helpers. `Shell.cs`: the "All apps" list and icons.
  - A choice of two or three values uses `Ui.Segmented` (the `.seg` buttons of the prototype). A choice of more values uses a combo box. The settings window shows no previews.
  - Command-line argument `--page <general|items|widgets|look|keys|about>` opens a page.
- `src/Assets/DashMaid.ico`: the application icon (the logo of the prototype). Both processes use it. To make it again, run `pwsh -NoProfile -File tools\MakeIcon.ps1`.
- `tools/`: scripts for the development. `Package.ps1` makes the release zip. `MakeIcon.ps1` makes the application icon.
- `tests/`: unit tests for the code that has no UI (`ToolbarTests`). Run them with `ctest --test-dir build -C Debug`.
- `build/`: CMake output. Git ignores this folder.

## Build

Run these commands from the repository root:

```
"C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" -S . -B build -G "Visual Studio 18 2026" -A x64
"C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build build --config Debug
```

The output is `build\src\Toolbar\Debug\DashMaid.exe`. The build also builds the settings window into `build\src\Toolbar\Debug\Settings\DashMaid.Settings.exe`: the `Settings` CMake target runs `dotnet build`. The first build downloads the Windows App SDK packages from NuGet. Only one instance runs at a time. To stop it, right-click the tray icon and select 退出.

To make the release zip, run this command from the repository root:

```
pwsh -NoProfile -File tools\Package.ps1
```

The script builds `DashMaid.exe` in the Release configuration, publishes the settings window with `dotnet publish`, and writes `build\DashMaid-<version>-win-x64.zip`. The version comes from `project()` in `CMakeLists.txt`. The zip has one folder, `DashMaid`, with `DashMaid.exe` and the `Settings` folder. The script removes the files that the app does not use at run time: `.pdb` and `.winmd` files, `createdump.exe`, `RestartAgent.exe`, and the WinUI language folders other than `zh-CN`, `zh-TW`, and `en-us`. The target computer needs no other installation: `DashMaid.exe` links the C++ runtime statically, and the settings window is self-contained and compiled with Native AOT (no .NET runtime files).

- The settings window references `Microsoft.WindowsAppSDK.WinUI`, not `Microsoft.WindowsAppSDK`. The second package adds the AI, ML, Search, and Widgets components, which the app does not use.
- `EnableMsixTooling` must stay `true` in the settings project. Without it, `dotnet publish` does not copy the `.pri` file, and the published settings window does not start.
- `dotnet publish` compiles the settings window with Native AOT (`PublishAot`). `dotnet build` and the `Settings` CMake target use the JIT. Native AOT needs `vswhere.exe` on `PATH` to find the linker; `Package.ps1` adds it. The code must stay safe for AOT: use `LibraryImport` and `[GeneratedComInterface]` for interop (not `[ComImport]` or `DllImport` with COM objects), mark classes that derive from WinRT types as `partial`, and keep the publish free of trim and AOT warnings.

## Configuration file

`config.json` (UTF-8). If `config.json` exists next to `DashMaid.exe`, the app uses that file. Else it uses `%APPDATA%\DashMaid\config.json`. The 常规 page of the settings window moves the file between the two folders (配置文件位置). It writes the new file before it deletes the old file, so that the toolbar always finds a file. If the folder of `DashMaid.exe` cannot be written, the file stays where it is. The toolbar writes the defaults to `%APPDATA%\DashMaid` when no file exists. Fields:

- `position`: `mode` (`float`, `dock`, `center`, `cursor`), `x` (toolbar center, screen pixels), `y` (toolbar top, screen pixels), `dockStyle` (`float`: a gap above the toolbar, or `flush`: no gap and square top corners), `appBar` (`true` or `false`: in the `dock` mode, keep maximized windows below the toolbar), `monitor` (the `dock` mode: the device path of the monitor, for example `\\?\DISPLAY#...`. Empty: the primary monitor. If the monitor is not connected, the toolbar uses the primary monitor).
- `theme`: `dark` or `light`. `material`: `solid` (default) or `acrylic`. `collapsedStyle`: `slice` or `glow`.
- `hideFullscreen`: `true` (default) or `false`. In the `float` and `dock` modes, hide the toolbar while a full-screen app runs on its monitor. The toolbar registers as an AppBar for the shell notification. This AppBar keeps no screen area.
- `autostart`: `true` or `false` (default). The toolbar writes or removes the `DashMaid` value in `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`.
- `widgetDisplay`: `text` (widgets always show their text) or `collapse` (widgets collapse like app items).
- `spring`: `stiffness`, `damping`.
- `hotkeys`: `run` (default `Ctrl+Alt+R`) and `toggle` (default ``Alt+` ``, registered only in the `center` and `cursor` modes). An empty string disables a hotkey.
- `items`: array of objects. The toolbar shows them in this order.
  - App: `type` (`app`), `name`, `path`, `args`, `workingDir`, `style` (`default`, `slice`, `glow`), `admin`.
  - Widget: `type` (`widget`), `source` (`clock`, `cpu`, `memory`, `network`, `command`), `click` (the file, URI, or program that a click opens. If `click` is empty, the toolbar uses the default of the source). A command widget also has `name`, `command` (runs through `cmd.exe /c`), `interval` (seconds), and optional `color` (`#RRGGBB`, text color in the dark theme).
- If the file exists but is not valid, the toolbar keeps the last valid settings and does not write the file.

## Required behavior

- Toolbar and item sizes: the sizes of the original component at 100% display scale. The toolbar is 72 px high at rest and 80 px high while it is open. Items are 8 × 24 px at rest, 12 × 36 px next to the item under the pointer, 10 × 30 px one item farther, and 48 px high when expanded. Items have a 24 px gap. Use fixed values. Do not read sizes from the system.
- Run button: a flat circle with a search icon. Do not use a 3D look.
- App items in both collapsed styles have a glow in the main color of the icon. The glow color is lighter or darker than the main color when necessary, so that items with black or white icons stay visible on the toolbar.
- Toolbar positions:
  - Floating: topmost in Z-order. The user can drag the toolbar to a new position.
  - Screen top: topmost and attached to the top edge. Optional AppBar registration keeps maximized windows below the toolbar.
  - Hidden until a global hotkey shows it at the center of the screen.
  - Hidden until a global hotkey shows it next to the pointer.
  - Monitor: the screen top mode uses the monitor that the user selects. The floating toolbar goes to the position where the user drags it. The two hotkey modes use the monitor of the pointer.
- App items: the user selects an app from the Start menu "All apps" list or selects an executable file and arguments manually. Windows has no public API for the "Pinned" list, so the settings window does not show it.
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
