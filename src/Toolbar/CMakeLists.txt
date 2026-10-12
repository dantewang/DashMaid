add_executable(DashMaid WIN32
    Main.cpp
    App.cpp
    Scene.cpp
    Config.cpp
    Icons.cpp
    Launch.cpp
    AppCatalog.cpp
    Hotkey.cpp
    Pinyin.cpp
    Search.cpp
    TextEdit.cpp
    DashMaid.manifest
)

target_precompile_headers(DashMaid PRIVATE pch.h)

target_compile_definitions(DashMaid PRIVATE
    UNICODE _UNICODE
    WIN32_LEAN_AND_MEAN NOMINMAX
    _WIN32_WINNT=0x0A00 NTDDI_VERSION=NTDDI_WIN10_NI
)

target_compile_options(DashMaid PRIVATE /W4 /permissive- /utf-8 /EHsc /Zc:__cplusplus /bigobj)

target_link_libraries(DashMaid PRIVATE
    windowsapp
    CoreMessaging
    d3d11 dxgi d2d1 dxguid
    windowscodecs dwmapi dcomp shlwapi
    dwrite imm32
)
