cmake_minimum_required(VERSION 3.16)

project(LzyDownloader VERSION 1.1.18 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Qt Configuration ---
# Enable Qt's automatic tools for MOC, RCC, and UIC
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Help CMake find Qt in common Windows SDK layouts when the IDE environment
# does not already provide Qt6_DIR/CMAKE_PREFIX_PATH (for example in CLion).
set(_qt_search_prefixes "")
set(_lzydownloader_using_vcpkg_toolchain OFF)

if(CMAKE_TOOLCHAIN_FILE MATCHES "vcpkg[\\/]+scripts[\\/]+buildsystems[\\/]+vcpkg\\.cmake$")
    set(_lzydownloader_using_vcpkg_toolchain ON)
endif()

if(NOT _lzydownloader_using_vcpkg_toolchain)
    foreach(_qt_env_var Qt6_DIR QT_DIR QTDIR)
        if(DEFINED ENV{${_qt_env_var}} AND NOT "$ENV{${_qt_env_var}}" STREQUAL "")
            if(EXISTS "$ENV{${_qt_env_var}}/Qt6Config.cmake")
                get_filename_component(_qt_prefix "$ENV{${_qt_env_var}}/../../.." ABSOLUTE)
                list(APPEND _qt_search_prefixes "${_qt_prefix}")
            elseif(EXISTS "$ENV{${_qt_env_var}}/lib/cmake/Qt6/Qt6Config.cmake")
                list(APPEND _qt_search_prefixes "$ENV{${_qt_env_var}}")
            endif()
        endif()
    endforeach()

    if(WIN32)
        # Check C:, D: (common for Dev Drives), and the drive the project is currently on
        string(SUBSTRING "${CMAKE_SOURCE_DIR}" 0 3 _project_drive)
        file(GLOB _qt_version_dirs LIST_DIRECTORIES true
            "C:/Qt/6.*"
            "D:/Qt/6.*"
            "${_project_drive}Qt/6.*"
        )
        foreach(_qt_version_dir IN LISTS _qt_version_dirs)
            foreach(_qt_arch_dir msvc2022_64 msvc2019_64 mingw_64)
                if(EXISTS "${_qt_version_dir}/${_qt_arch_dir}/lib/cmake/Qt6/Qt6Config.cmake")
                    list(APPEND _qt_search_prefixes "${_qt_version_dir}/${_qt_arch_dir}")
                endif()
            endforeach()
        endforeach()
    endif()
endif()

if(_qt_search_prefixes)
    list(REMOVE_DUPLICATES _qt_search_prefixes)
    list(APPEND CMAKE_PREFIX_PATH ${_qt_search_prefixes})
    message(STATUS "Additional Qt search prefixes: ${_qt_search_prefixes}")
endif()

# Find the required Qt6 packages
find_package(Qt6 REQUIRED COMPONENTS Widgets Network Sql Core Concurrent)

# --- Source Files ---
# Use file(GLOB_RECURSE) to automatically find all source and header files.
# This is the key fix: it ensures CMAKE_AUTOMOC can find all Q_OBJECT macros.
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS
    "src/*.cpp"
    "src/*.h"
)

# Filter out specific duplicate implementations to avoid ODR/link conflicts.
# The aria2 RPC daemon, yt-dlp download-info extractor, and ffmpeg muxer now
# live in `src/core/download_pipeline/`, so we exclude the older config-driven
# counterparts that still use the same class names in `src/core/`.
set(EXCLUDED_SOURCES_REGEXES
    ".*/src/core/YtDlpJsonExtractor\\.(cpp|h)$"
    ".*/src/core/Aria2Daemon\\.(cpp|h)$"
    ".*/src/core/FfmpegPostProcessor\\.(cpp|h)$"
)

foreach(REGEX_PATTERN IN LISTS EXCLUDED_SOURCES_REGEXES)
    list(FILTER SOURCES EXCLUDE REGEX "${REGEX_PATTERN}")
endforeach()


# --- Include Directories ---
# This ensures that #include "utils/..." or "core/..." works regardless of
# which directory the source file is located in.
include_directories(
    ${CMAKE_CURRENT_BINARY_DIR}/src   # Generated headers first (version.h, etc.)
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}       # Helps the RC compiler find relative paths like "src/resources/icon.ico"
)

# --- Version Header ---
# Generate version.h from the project VERSION so every build has a single
# canonical version string shared by the executable, the .rc file, and code.
configure_file(
    src/core/version.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/src/core/version.h
    @ONLY
)

# Generate the .rc file from a template so the RC compiler never sees
# C++ Qt headers (on Windows version.h matches Version.h case-insensitively).
configure_file(
    LzyDownloader.rc.in
    ${CMAKE_CURRENT_BINARY_DIR}/LzyDownloader.rc
    @ONLY
)

# --- Executable Target ---
add_executable(LzyDownloader
    # Use WIN32 for GUI applications on Windows to avoid a console window
    WIN32

    # Application entry point
    main.cpp

    # All source and header files gathered by the glob
    ${SOURCES}

    # Explicitly include recently added files so link-time availability
    # does not depend on whether the generator re-ran after they were created.
    src/core/ProcessUtils.cpp
    src/core/LocalApiServer.cpp
    src/ui/RuntimeSelectionDialog.cpp
    src/ui/FormatSelectionDialog.cpp
    src/ui/DownloadSectionsDialog.cpp
    src/ui/MissingBinariesDialog.cpp
    src/ui/advanced_settings/BinariesPage.cpp
    src/ui/SupportedSitesDialog.cpp

    # Qt resource file for icons, etc.
    src/ui/resources.qrc

    # Windows application icon and version metadata (generated from template)
    ${CMAKE_CURRENT_BINARY_DIR}/LzyDownloader.rc
)

# --- Link Libraries ---
target_link_libraries(LzyDownloader PRIVATE
    Qt6::Widgets
    Qt6::Network
    Qt6::Sql
    Qt6::Core
    Qt6::Concurrent
)

if(MSVC)
    target_compile_options(LzyDownloader PRIVATE /FS)
endif()

# --- Deployment of Resources ---
# This section ensures that required data files are copied to the build
# output directory, so the application can find them at runtime.
add_custom_command(TARGET LzyDownloader POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/extractors_yt-dlp.json"
    "$<TARGET_FILE_DIR:LzyDownloader>"
    COMMENT "Copying yt-dlp extractor list to output directory")
add_custom_command(TARGET LzyDownloader POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/extractors_gallery-dl.json"
    "$<TARGET_FILE_DIR:LzyDownloader>"
    COMMENT "Copying gallery-dl extractor list to output directory")

# --- Windows Specific Deployment (Qt Plugins) ---
# This ensures that Qt plugins (for database, image formats, TLS, etc.)
# are available in the build directory, which is critical for both
# Debug and Release builds to function correctly.
if(WIN32)
    set(_lzydownloader_can_copy_vcpkg_qt_plugins OFF)
    if(_lzydownloader_using_vcpkg_toolchain AND DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
        set(_lzydownloader_can_copy_vcpkg_qt_plugins ON)
    endif()

    if(_lzydownloader_can_copy_vcpkg_qt_plugins)
        # Vcpkg handles the core Qt DLLs via applocal, but the app still needs the
        # runtime plugins beside the executable. Copy them directly from the active
        # vcpkg tree rather than relying on windeployqt's standalone-Qt assumptions.
        foreach(_plugin_dir IN ITEMS platforms sqldrivers tls imageformats styles)
            set(_plugin_release_dir "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/Qt6/plugins/${_plugin_dir}")
            set(_plugin_debug_dir "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/Qt6/plugins/${_plugin_dir}")
            file(GLOB _plugin_release_dlls RELATIVE "${_plugin_release_dir}" "${_plugin_release_dir}/*.dll")

            foreach(_plugin_release_dll IN LISTS _plugin_release_dlls)
                string(REGEX REPLACE "\\.dll$" "d.dll" _plugin_debug_dll "${_plugin_release_dll}")
                if(EXISTS "${_plugin_debug_dir}/${_plugin_debug_dll}")
                    add_custom_command(TARGET LzyDownloader POST_BUILD
                        COMMAND ${CMAKE_COMMAND} -E make_directory
                        "$<TARGET_FILE_DIR:LzyDownloader>/${_plugin_dir}"
                        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "$<$<CONFIG:Debug>:${_plugin_debug_dir}/${_plugin_debug_dll}>$<$<NOT:$<CONFIG:Debug>>:${_plugin_release_dir}/${_plugin_release_dll}>"
                        "$<TARGET_FILE_DIR:LzyDownloader>/${_plugin_dir}/$<$<CONFIG:Debug>:${_plugin_debug_dll}>$<$<NOT:$<CONFIG:Debug>>:${_plugin_release_dll}>"
                        COMMENT "Copying Qt plugin: ${_plugin_dir}/${_plugin_release_dll}"
                    )
                endif()
            endforeach()
        endforeach()

        # The OpenSSL TLS backend plugin is present in the deployed Qt plugin set,
        # but it still needs the OpenSSL runtime DLLs beside the executable.
        foreach(_openssl_runtime IN ITEMS libcrypto-3-x64.dll libssl-3-x64.dll)
            add_custom_command(TARGET LzyDownloader POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<$<CONFIG:Debug>:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/bin/${_openssl_runtime}>$<$<NOT:$<CONFIG:Debug>>:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin/${_openssl_runtime}>"
                "$<TARGET_FILE_DIR:LzyDownloader>/${_openssl_runtime}"
                COMMENT "Copying OpenSSL runtime: ${_openssl_runtime}"
            )
        endforeach()
    else()
        # Find windeployqt from Qt6::qmake location
        get_target_property(_qmake_executable Qt6::qmake IMPORTED_LOCATION)
        get_filename_component(_qt_bin_dir "${_qmake_executable}" DIRECTORY)
        find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${_qt_bin_dir}")

        if(WINDEPLOYQT_EXECUTABLE)
            add_custom_command(TARGET LzyDownloader POST_BUILD
                COMMAND "${WINDEPLOYQT_EXECUTABLE}"
                $<$<CONFIG:Debug>:--debug>
                $<$<NOT:$<CONFIG:Debug>>:--release>
                --dir "$<TARGET_FILE_DIR:LzyDownloader>"
                --no-translations
                --no-compiler-runtime
                --no-opengl-sw
                "$<TARGET_FILE:LzyDownloader>"
                COMMENT "Running windeployqt to copy Qt dependencies..."
            )

            # Some environments end up with mixed Qt runtimes in the build output
            # even though CMake resolved the official Qt installation above.
            # Re-copy the primary Qt DLLs from the configured Qt bin directory so
            # the release folder cannot retain an incompatible alternate build.
            foreach(_qt_runtime IN ITEMS
                Qt6Core.dll
                Qt6Gui.dll
                Qt6Network.dll
                Qt6Sql.dll
                Qt6Svg.dll
                Qt6Widgets.dll
            )
                add_custom_command(TARGET LzyDownloader POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_qt_bin_dir}/${_qt_runtime}"
                    "$<TARGET_FILE_DIR:LzyDownloader>/${_qt_runtime}"
                    COMMENT "Re-copying ${_qt_runtime} from configured Qt runtime"
                )
            endforeach()

            foreach(_openssl_runtime IN ITEMS libcrypto-3-x64.dll libssl-3-x64.dll)
                if(EXISTS "${_qt_bin_dir}/${_openssl_runtime}")
                    add_custom_command(TARGET LzyDownloader POST_BUILD
                        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${_qt_bin_dir}/${_openssl_runtime}"
                        "$<TARGET_FILE_DIR:LzyDownloader>/${_openssl_runtime}"
                        COMMENT "Copying OpenSSL runtime: ${_openssl_runtime}"
                    )
                endif()
            endforeach()

        else()
            message(WARNING "windeployqt not found, Qt dependencies will not be copied automatically.")
        endif()
    endif()
endif()
