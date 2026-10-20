cmake_minimum_required(VERSION 3.20)

project(Bolt
    VERSION 0.1.2
    DESCRIPTION "Clone of a fast native Windows file manager, reconstructed from static binary analysis"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

add_executable(Bolt WIN32
    src/bolt.rc
    src/resource.h
    src/main.cpp
    src/app_ui.cpp
    src/theme.cpp
    src/theme.h
    src/view_sidebar.cpp
    src/tabs.cpp
    src/view_titlebar.cpp
    src/view_list.cpp
    src/view_list.h
    src/view_inspector.cpp
    src/view_inspector.h
    src/view_statusbar.cpp
    src/view_statusbar.h
    src/view_context_menu.cpp
    src/view_context_menu.h
    src/view_menu.cpp
    src/view_menu.h
    src/view_options.cpp
    src/view_options.h
    src/app_actions.h
    src/commands.cpp
    src/commands.h
    src/input_targets.cpp
    src/input_targets.h
    src/keymap.cpp
    src/keymap.h
    src/ui_renderer.cpp
    src/ui_renderer.h
    src/d3d11_renderer.cpp
    src/d3d11_renderer.h
    src/font_atlas.cpp
    src/font_atlas.h
    src/image_preview.cpp
    src/image_preview.h
    src/shell_icons.cpp
    src/shell_icons.h
    src/navigation.cpp
    src/navigation.h
    src/recents.cpp
    src/recents.h
    src/home.cpp
    src/home.h
    src/update.cpp
    src/update.h
    src/settings.cpp
    src/settings.h
    src/shell_menu.cpp
    src/shell_menu.h
    src/shell_location.cpp
    src/shell_location.h
    src/ntfs_attributes.cpp
    src/ntfs_attributes.h
    src/shell_new.cpp
    src/shell_new.h
    src/shell_preview.cpp
    src/shell_preview.h
    src/text_view.cpp
    src/text_view.h
    src/shell_sendto.cpp
    src/shell_sendto.h
    src/diag.cpp
    src/diag.h
    src/dir_stats.cpp
    src/dir_watch.cpp
    src/dir_stats.h
    src/drag_drop.cpp
    src/drag_drop.h
    src/file_ops.cpp
    src/file_ops.h
    src/language.cpp
    src/language.h
    src/gl_loader.cpp
    src/gl_loader.h)

target_include_directories(Bolt PRIVATE src)

# The shader directory is passed in as a compile definition so development builds
# read the shaders straight from the source tree and can be iterated on without
# rebuilding.
target_compile_definitions(Bolt PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE
    _UNICODE
    REA_SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/src/shaders"
    # La versione si legge qui e si passa al codice, cosi' non e' scritta due volte:
    # la usano l'installer e il controllo degli aggiornamenti.
    BOLT_VERSION="${PROJECT_VERSION}")

# opengl32 provides the OpenGL 1.1 entry points; everything newer is resolved at
# runtime through wglGetProcAddress.
# shell32 + ole32 + uuid provide the shell icon, thumbnail and known-folder APIs:
# the FOLDERID_* identifiers live in uuid.lib.
# shlwapi provides SHLoadIndirectString, which resolves the "@dll,-id" labels the
# shell stores in the registry for the "New" menu types. RegLoadMUIString, which
# would need no new library, returns an error on the same strings and the names
# come out in English.
# propsys reads the shell's property values (VariantToUInt64, VariantToFileTime):
# namespace folders like the recycle bin have no file attributes to read, and their
# size and date come from IShellFolder2::GetDetailsEx as PROPVARIANTs.
# windowscodecs decodes the image for the inspector's large preview (WIC): the shell
# thumbnail is 128 px, and its own decoder is the one that knows every format
# Windows can open.
target_link_libraries(Bolt PRIVATE opengl32 gdi32 user32 shell32 ole32 uuid shlwapi dwmapi propsys windowscodecs d3d11 dxgi d3dcompiler winhttp bcrypt)

if(MSVC)
    target_compile_options(Bolt PRIVATE /W4 /permissive- /utf-8)
    # Static CRT keeps the executable self-contained, matching the reference
    # application's standalone single-file property.
    set_property(TARGET Bolt PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
    target_compile_options(Bolt PRIVATE -Wall -Wextra)
endif()

# Also stage the shaders next to the binary so a copied build directory stays
# runnable.
add_custom_command(TARGET Bolt POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/src/shaders"
        "$<TARGET_FILE_DIR:Bolt>/shaders"
    COMMENT "Staging shaders next to the executable")
