cmake_minimum_required(VERSION 3.16)
project(LzyDownloader
    VERSION 1.2.32
    DESCRIPTION "Qt desktop video, audio, playlist, and gallery downloader powered by yt-dlp and gallery-dl"
    HOMEPAGE_URL "https://github.com/vincentwetzel/lzy-downloader"
    LANGUAGES CXX
)
enable_testing()

set(_qt_search_prefixes "")
set(_lzydownloader_using_vcpkg_toolchain OFF)

if(DEFINED CMAKE_TOOLCHAIN_FILE AND CMAKE_TOOLCHAIN_FILE MATCHES "vcpkg[\\/]+scripts[\\/]+buildsystems[\\/]+vcpkg\\.cmake$")
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

find_package(Qt6 COMPONENTS Core Gui Widgets Network Sql Test Concurrent REQUIRED)

if(UNIX AND NOT APPLE)
    find_package(Qt6 COMPONENTS DBus REQUIRED)
endif()

# A MinGW driver launched by an absolute path still needs its sibling bin
# directory on PATH when it starts cc1plus. This is common for Qt's bundled
# MinGW toolchain, whose runtime DLLs are not in the system PATH by default.
if(WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    get_filename_component(_lzy_mingw_bin_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
    set(CMAKE_CXX_COMPILER_LAUNCHER
        "${CMAKE_COMMAND};-E;env;PATH=${_lzy_mingw_bin_dir}")
endif()

option(LZY_SKIP_QT_DEPLOY "Skip Qt runtime deployment post-build steps for local developer builds." OFF)

if(WIN32)
    function(lzy_add_qt_windows_deploy target_name)
        if(LZY_SKIP_QT_DEPLOY)
            if(DEFINED Qt6_DIR)
                get_filename_component(_qt_prefix "${Qt6_DIR}/../../.." ABSOLUTE)
                set(_qt_bin_dir "${_qt_prefix}/bin")
                set(_qt_plugins_dir "${_qt_prefix}/plugins")
                foreach(_runtime_dll
                    Qt6Core.dll
                    Qt6Gui.dll
                    Qt6Widgets.dll
                    Qt6Network.dll
                    Qt6Sql.dll
                    Qt6Concurrent.dll
                    Qt6Test.dll
                    libgcc_s_seh-1.dll
                    libstdc++-6.dll
                    libwinpthread-1.dll
                )
                    if(EXISTS "${_qt_bin_dir}/${_runtime_dll}")
                        add_custom_command(TARGET ${target_name} POST_BUILD
                            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                                "${_qt_bin_dir}/${_runtime_dll}"
                                "$<TARGET_FILE_DIR:${target_name}>"
                            VERBATIM
                        )
                    endif()
                endforeach()

                foreach(_plugin_dir platforms sqldrivers tls imageformats iconengines networkinformation styles)
                    if(EXISTS "${_qt_plugins_dir}/${_plugin_dir}")
                        add_custom_command(TARGET ${target_name} POST_BUILD
                            COMMAND "${CMAKE_COMMAND}" -E copy_directory
                                "${_qt_plugins_dir}/${_plugin_dir}"
                                "$<TARGET_FILE_DIR:${target_name}>/${_plugin_dir}"
                            VERBATIM
                        )
                    endif()
                endforeach()

                set(_qt_minimal_plugin "${_qt_prefix}/plugins/platforms/qminimal.dll")
                if(EXISTS "${_qt_minimal_plugin}")
                    add_custom_command(TARGET ${target_name} POST_BUILD
                        COMMAND "${CMAKE_COMMAND}" -E make_directory
                            "$<TARGET_FILE_DIR:${target_name}>/platforms"
                        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                            "${_qt_minimal_plugin}"
                            "$<TARGET_FILE_DIR:${target_name}>/platforms/qminimal.dll"
                        COMMENT "Deploying Qt minimal platform for ${target_name}"
                        VERBATIM
                    )
                endif()
            endif()
            return()
        endif()

        if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
            set(qt_plugins_dir "$<$<CONFIG:Debug>:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/Qt6/plugins>$<$<NOT:$<CONFIG:Debug>>:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/Qt6/plugins>")
            set(qt_runtime_dir "$<$<CONFIG:Debug>:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/bin>$<$<NOT:$<CONFIG:Debug>>:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin>")
            add_custom_command(TARGET ${target_name} POST_BUILD
                COMMAND "${CMAKE_COMMAND}"
                    -DQT_PLUGINS_SOURCE=${qt_plugins_dir}
                    -DQT_PLUGINS_DEST=$<TARGET_FILE_DIR:${target_name}>
                    -DQT_RUNTIME_DIR=${qt_runtime_dir}
                    -DTARGET_RUNTIME_DIR=$<TARGET_FILE_DIR:${target_name}>
                    -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/deploy_openssl_runtime.cmake"
                COMMENT "Copying Qt runtime plugins for ${target_name}"
                USES_TERMINAL
                VERBATIM
            )

            get_filename_component(_mingw_runtime_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
            foreach(_mingw_runtime_dll libgcc_s_seh-1.dll libstdc++-6.dll libwinpthread-1.dll)
                if(EXISTS "${_mingw_runtime_dir}/${_mingw_runtime_dll}")
                    add_custom_command(TARGET ${target_name} POST_BUILD
                        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                            "${_mingw_runtime_dir}/${_mingw_runtime_dll}"
                            "$<TARGET_FILE_DIR:${target_name}>"
                        VERBATIM
                    )
                endif()
            endforeach()
        else()
            find_program(WINDEPLOYQT_EXECUTABLE NAMES windeployqt windeployqt6)
        endif()

        if(NOT DEFINED VCPKG_INSTALLED_DIR AND WINDEPLOYQT_EXECUTABLE)
            add_custom_command(TARGET ${target_name} POST_BUILD
                COMMAND "${WINDEPLOYQT_EXECUTABLE}"
                    --no-translations
                    --no-compiler-runtime
                    "$<$<CONFIG:Debug>:--debug>$<$<NOT:$<CONFIG:Debug>>:--release>"
                    "$<TARGET_FILE:${target_name}>"
                COMMENT "Deploying Qt runtime plugins for ${target_name}"
                VERBATIM
            )
        elseif(NOT DEFINED VCPKG_INSTALLED_DIR)
            message(WARNING "windeployqt was not found; ${target_name} may fail to start without Qt plugins in the output directory.")
        endif()

        # The non-vcpkg Qt deployment normally omits the headless platform
        # plugin used by the test suite, so copy it when available.
        if(DEFINED Qt6_DIR)
            get_filename_component(_qt_prefix "${Qt6_DIR}/../../.." ABSOLUTE)
            set(_qt_minimal_plugin "${_qt_prefix}/plugins/platforms/qminimal.dll")
            if(EXISTS "${_qt_minimal_plugin}")
                add_custom_command(TARGET ${target_name} POST_BUILD
                    COMMAND "${CMAKE_COMMAND}" -E make_directory
                        "$<TARGET_FILE_DIR:${target_name}>/platforms"
                    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                        "${_qt_minimal_plugin}"
                        "$<TARGET_FILE_DIR:${target_name}>/platforms/qminimal.dll"
                    COMMENT "Deploying Qt minimal platform for ${target_name}"
                    VERBATIM
                )
            endif()
        endif()

        set(_openssl_runtime_dir "")
        if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
            set(_openssl_runtime_dir "$<$<CONFIG:Debug>:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/bin>$<$<NOT:$<CONFIG:Debug>>:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin>")
        elseif(DEFINED Qt6_DIR)
            get_filename_component(_qt_prefix "${Qt6_DIR}/../../.." ABSOLUTE)
            set(_openssl_runtime_dir "${_qt_prefix}/bin")
        endif()

        if(_openssl_runtime_dir)
            add_custom_command(TARGET ${target_name} POST_BUILD
                COMMAND "${CMAKE_COMMAND}"
                    -DOPENSSL_RUNTIME_DIR=${_openssl_runtime_dir}
                    -DTARGET_RUNTIME_DIR=$<TARGET_FILE_DIR:${target_name}>
                    -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/deploy_openssl_runtime.cmake"
                COMMENT "Copying OpenSSL runtime DLLs for ${target_name}"
                VERBATIM
            )
        else()
            message(WARNING "OpenSSL runtime DLLs were not found; Qt HTTPS requests may fail if the OpenSSL TLS backend is deployed without libssl/libcrypto.")
        endif()
    endfunction()
else()
    function(lzy_add_qt_windows_deploy target_name)
    endfunction()
endif()

# Set global CMake properties for AUTOMOC, AUTOUIC, AUTORCC
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
# CMake's optional compiler-predefines probe can fail with EPERM on Windows
# when the MinGW compiler is launched through libuv. Qt's moc receives the
# target compile definitions directly, so the generated predefines header is
# not required for this project.
set(CMAKE_AUTOMOC_COMPILER_PREDEFINES OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/version.h.in
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/version.h
    @ONLY
)

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/LzyDownloader.rc.in
    ${CMAKE_CURRENT_BINARY_DIR}/LzyDownloader.rc
    @ONLY
)

# Define LzyAppLib static library
add_library(LzyAppLib STATIC
    src/core/AppUpdater.cpp
    src/core/ArchiveManager.cpp
    src/core/BaseBinaryUpdater.cpp
    src/core/ConfigManager.cpp
    src/core/DownloadFinalizer.cpp
    src/core/FileReplacement.cpp
    src/core/DownloadManager.cpp
    src/core/DownloadManagerControls.cpp
    src/core/DownloadManagerEnqueue.cpp
    src/core/DownloadManagerExecution.cpp
    src/core/DownloadManagerPlaylist.cpp
    src/core/DownloadManagerWorkers.cpp
    src/core/DownloadQueueManager.cpp
    src/core/DownloadQueueManagerCleanup.cpp
    src/core/DownloadQueueManagerRecovery.cpp
    src/core/DownloadQueueState.cpp
    src/core/DownloadTempCleanup.cpp
    src/core/GalleryDlArgsBuilder.cpp
    src/core/GalleryDlWorker.cpp
    src/core/LocalApiServer.cpp
    src/core/MetadataEmbedder.cpp
    src/core/PlaylistExpansionParser.cpp
    src/core/PlaylistExpansionWorker.cpp
    src/core/PlaylistRangeDialog.cpp
    src/core/ProcessUtils.cpp
    src/core/PowerInhibitor.cpp
    src/core/SortingManager.cpp
    src/core/StartupWorker.cpp
    src/core/UrlValidator.cpp
    src/core/YtDlpArgsBuilder.cpp
    src/core/YtDlpWorker.cpp
    src/core/YtDlpWorkerDiagnostics.cpp
    src/core/YtDlpWorkerOutput.cpp
    src/core/YtDlpWorkerProcess.cpp
    src/core/YtDlpWorkerProgress.cpp
    src/core/YtDlpWorkerTransfers.cpp
    src/core/download_pipeline/Aria2DownloadWorker.cpp
    src/core/download_pipeline/Aria2RpcClient.cpp
    src/core/download_pipeline/FfmpegMuxer.cpp
    src/core/download_pipeline/YtDlpDownloadInfoExtractor.cpp
    src/utils/BinaryFinder.cpp
    src/utils/BrowserUtils.cpp
    src/utils/ExtractorJsonParser.cpp
    src/utils/LogManager.cpp
    src/ui/ActiveDownloadsTab.cpp
    src/ui/AdvancedSettingsTab.cpp
    src/ui/DownloadHistoryTab.cpp
    src/ui/DownloadItemWidget.cpp
    src/ui/DownloadSectionsDialog.cpp
    src/ui/FormatSelectionDialog.cpp
    src/ui/MainWindow.cpp
    src/ui/MainWindowConnections.cpp
    src/ui/MainWindowDiscord.cpp
    src/ui/MainWindowDownloads.cpp
    src/ui/MainWindowUi.cpp
    src/ui/MainWindowUiBuilder.cpp
    src/ui/InitialBinarySetupDialog.cpp
    src/ui/MissingBinariesDialog.cpp
    src/ui/RuntimeSelectionDialog.cpp
    src/ui/SortingRuleDialog.cpp
    src/ui/SortingTab.cpp
    src/ui/StartTab.cpp
    src/ui/StartTabUiBuilder.cpp
    src/ui/SupportedSitesDialog.cpp
    src/ui/ToggleSwitch.cpp
    src/ui/advanced_settings/AudioSettingsPage.cpp
    src/ui/advanced_settings/AuthenticationPage.cpp
    src/ui/advanced_settings/BinariesPage.cpp
    src/ui/advanced_settings/ConfigurationPage.cpp
    src/ui/advanced_settings/DownloadOptionsPage.cpp
    src/ui/advanced_settings/LivestreamSettingsPage.cpp
    src/ui/advanced_settings/MetadataPage.cpp
    src/ui/advanced_settings/OutputTemplatesPage.cpp
    src/ui/advanced_settings/SubtitlesPage.cpp
    src/ui/advanced_settings/VideoSettingsPage.cpp
    src/ui/start_tab/StartTabCommandPreviewUpdater.cpp
    src/ui/start_tab/StartTabDownloadActions.cpp
    src/ui/start_tab/StartTabUrlHandler.cpp
)

target_include_directories(LzyAppLib PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/download_pipeline
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/advanced_settings
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/start_tab
    ${CMAKE_CURRENT_BINARY_DIR}
)

target_link_libraries(LzyAppLib PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    Qt6::Sql
    Qt6::Concurrent
)

if(UNIX AND NOT APPLE)
    target_link_libraries(LzyAppLib PRIVATE Qt6::DBus)
elseif(APPLE)
    find_library(LZY_IOKIT_FRAMEWORK IOKit REQUIRED)
    target_link_libraries(LzyAppLib PRIVATE "${LZY_IOKIT_FRAMEWORK}")
endif()

# Main application executable
add_executable(LzyDownloader
    WIN32
    main.cpp
    src/ui/resources.qrc
)

if(WIN32)
    target_sources(LzyDownloader PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/LzyDownloader.rc)
endif()

if(APPLE)
    set_target_properties(LzyDownloader PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "LzyDownloader"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.lzydownloader.app"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    )
endif()

target_link_libraries(LzyDownloader PRIVATE
    LzyAppLib
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    Qt6::Sql
)
lzy_add_qt_windows_deploy(LzyDownloader)

if(MSVC)
    target_compile_options(LzyAppLib PRIVATE /FS)
    target_compile_options(LzyDownloader PRIVATE /FS)
endif()

add_custom_command(TARGET LzyDownloader POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/extractors_yt-dlp.json"
    "${CMAKE_CURRENT_SOURCE_DIR}/extractors_gallery-dl.json"
    "$<TARGET_FILE_DIR:LzyDownloader>"
    COMMENT "Copying extractor lists to output directory"
)

function(lzy_add_test target_name source_file)
    add_executable(${target_name} tests/BaseTest.cpp ${source_file})
    
    target_include_directories(${target_name} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
        ${CMAKE_CURRENT_SOURCE_DIR}/src/core
    )
    
    target_link_libraries(${target_name} PRIVATE
        LzyAppLib
        Qt6::Test Qt6::Core Qt6::Network Qt6::Sql Qt6::Widgets Qt6::Concurrent
    )
    
    if(MSVC)
        target_compile_options(${target_name} PRIVATE /FS)
    endif()
    
    lzy_add_qt_windows_deploy(${target_name})
    add_test(NAME ${target_name} COMMAND ${target_name})
    # Qt's vcpkg MinGW builds provide the headless qminimal plugin, whereas
    # qoffscreen is not consistently packaged. minimal also supports QWidget
    # tests without requiring a display server.
    set_tests_properties(${target_name} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=minimal"
    )
endfunction()

lzy_add_test(TestYtDlpArgsBuilder tests/TestYtDlpArgsBuilder.cpp)
lzy_add_test(TestYtDlpWorker tests/TestYtDlpWorker.cpp)
lzy_add_test(TestEndToEnd tests/TestEndToEnd.cpp)
lzy_add_test(TestArchiveManager tests/TestArchiveManager.cpp)
lzy_add_test(TestConfigManager tests/TestConfigManager.cpp)
lzy_add_test(TestLocalApiServer tests/TestLocalApiServer.cpp)
lzy_add_test(TestUrlValidator tests/TestUrlValidator.cpp)
lzy_add_test(TestSortingManager tests/TestSortingManager.cpp)
lzy_add_test(TestUIWidgets tests/TestUIWidgets.cpp)
lzy_add_test(TestProcessUtils tests/TestProcessUtils.cpp)
lzy_add_test(TestPlaylistRangeDialog tests/TestPlaylistRangeDialog.cpp)
lzy_add_test(TestDownloadManager tests/TestDownloadManager.cpp)
lzy_add_test(TestGalleryDlArgsBuilder tests/TestGalleryDlArgsBuilder.cpp)
lzy_add_test(TestPlaylistExpansionParser tests/TestPlaylistExpansionParser.cpp)
lzy_add_test(TestDownloadQueueState tests/TestDownloadQueueState.cpp)
lzy_add_test(TestDownloadQueueManager tests/TestDownloadQueueManager.cpp)
lzy_add_test(TestFileReplacement tests/TestFileReplacement.cpp)
lzy_add_test(TestDownloadTempCleanup tests/TestDownloadTempCleanup.cpp)
lzy_add_test(TestPowerInhibitor tests/TestPowerInhibitor.cpp)
