cmake_minimum_required(VERSION 3.28)
project(DashMaid VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Link the C++ runtime statically, so that DashMaid.exe does not need the
# Visual C++ Redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

enable_testing()

add_subdirectory(src/Toolbar)
add_subdirectory(tests)

# The settings window (C#, WinUI 3). It builds into the "Settings" folder next to DashMaid.exe.
find_program(DOTNET dotnet REQUIRED)
add_custom_target(Settings ALL
    # The C++ build sets LIB for the linker. The C# compiler warns about its folders.
    COMMAND ${CMAKE_COMMAND} -E env --unset=LIB
            ${DOTNET} build "${CMAKE_SOURCE_DIR}/src/Settings/DashMaid.Settings.csproj"
            -c $<CONFIG> --nologo -v:q
            "-p:OutputPath=$<TARGET_FILE_DIR:DashMaid>/Settings/"
            "-p:BaseIntermediateOutputPath=${CMAKE_BINARY_DIR}/src/Settings/obj/"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/src/Settings"
    COMMENT "Building the settings window"
    VERBATIM)
