add_executable(ToolbarTests
    ToolbarTests.cpp
    ../src/Toolbar/Hotkey.cpp
    ../src/Toolbar/Paint.cpp
    ../src/Toolbar/Pinyin.cpp
    ../src/Toolbar/Search.cpp
    ../src/Toolbar/TextEdit.cpp
)
target_compile_definitions(ToolbarTests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(ToolbarTests PRIVATE /W4 /permissive- /utf-8 /EHsc)
target_link_libraries(ToolbarTests PRIVATE d2d1 dxguid windowsapp)
add_test(NAME ToolbarTests COMMAND ToolbarTests)
