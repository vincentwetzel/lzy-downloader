cmake_minimum_required(VERSION 3.16)
project(LzyDownloader VERSION 1.1.38 LANGUAGES CXX)
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

if(WIN32)
    function(lzy_add_qt_windows_deploy target_name)
        if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
            set(qt_plugins_dir "$<$<CONFIG:Debug>:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/Qt6/plugins>$<$<NOT:$<CONFIG:Debug>>:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/Qt6/plugins>")
            add_custom_command(TARGET ${target_name} POST_BUILD
                COMMAND "${CMAKE_COMMAND}" -E copy_directory
                    "${qt_plugins_dir}"
                    "$<TARGET_FILE_DIR:${target_name}>"
                COMMENT "Copying Qt runtime plugins for ${target_name}"
                VERBATIM
            )
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
    src/core/Aria2Daemon.cpp
    src/core/ConfigManager.cpp
    src/core/DownloadFinalizer.cpp
    src/core/DownloadManager.cpp
    src/core/DownloadManagerControls.cpp
    src/core/DownloadManagerEnqueue.cpp
    src/core/DownloadManagerExecution.cpp
    src/core/DownloadManagerPlaylist.cpp
    src/core/DownloadManagerWorkers.cpp
    src/core/DownloadQueueManager.cpp
    src/core/DownloadQueueState.cpp
    src/core/FfmpegPostProcessor.cpp
    src/core/GalleryDlArgsBuilder.cpp
    src/core/GalleryDlUpdater.cpp
    src/core/GalleryDlWorker.cpp
    src/core/LocalApiServer.cpp
    src/core/MetadataEmbedder.cpp
    src/core/PlaylistExpander.cpp
    src/core/ProcessUtils.cpp
    src/core/SortingManager.cpp
    src/core/StartupWorker.cpp
    src/core/UrlValidator.cpp
    src/core/YtDlpArgsBuilder.cpp
    src/core/YtDlpJsonExtractor.cpp
    src/core/YtDlpUpdater.cpp
    src/core/YtDlpWorker.cpp
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
    src/utils/StringUtils.cpp
    src/utils/YtDlpJsonParser.cpp
    src/ui/ActiveDownloadsTab.cpp
    src/ui/AdvancedSettingsTab.cpp
    src/ui/DownloadItemWidget.cpp
    src/ui/DownloadSectionsDialog.cpp
    src/ui/FormatSelectionDialog.cpp
    src/ui/MainWindow.cpp
    src/ui/MainWindowConnections.cpp
    src/ui/MainWindowDiscord.cpp
    src/ui/MainWindowDownloads.cpp
    src/ui/MainWindowUi.cpp
    src/ui/MainWindowUiBuilder.cpp
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

# Main application executable
add_executable(LzyDownloader
    WIN32
    main.cpp
    src/ui/resources.qrc
)

if(WIN32)
    target_sources(LzyDownloader PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/LzyDownloader.rc)
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
    "$<TARGET_FILE_DIR:LzyDownloader>"
    COMMENT "Copying yt-dlp extractor list to output directory"
)
add_custom_command(TARGET LzyDownloader POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/extractors_gallery-dl.json"
    "$<TARGET_FILE_DIR:LzyDownloader>"
    COMMENT "Copying gallery-dl extractor list to output directory"
)

add_executable(TestYtDlpArgsBuilderTest
    src/tests/BaseTest.cpp
    src/tests/TestYtDlpArgsBuilder.cpp
)

target_include_directories(TestYtDlpArgsBuilderTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tests
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core
)

target_link_libraries(TestYtDlpArgsBuilderTest PRIVATE
    LzyAppLib
    Qt6::Test Qt6::Core Qt6::Network Qt6::Sql Qt6::Widgets
)
if(MSVC)
    target_compile_options(TestYtDlpArgsBuilderTest PRIVATE /FS)
endif()
lzy_add_qt_windows_deploy(TestYtDlpArgsBuilderTest)
add_test(NAME TestYtDlpArgsBuilderTest COMMAND TestYtDlpArgsBuilderTest)

add_executable(TestYtDlpWorkerTest
    src/tests/BaseTest.cpp
    src/tests/TestYtDlpWorker.cpp
)

target_include_directories(TestYtDlpWorkerTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tests
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core
)

target_link_libraries(TestYtDlpWorkerTest PRIVATE
    LzyAppLib
    Qt6::Test Qt6::Core Qt6::Network Qt6::Sql Qt6::Widgets
)
if(MSVC)
    target_compile_options(TestYtDlpWorkerTest PRIVATE /FS)
endif()
lzy_add_qt_windows_deploy(TestYtDlpWorkerTest)
add_test(NAME TestYtDlpWorkerTest COMMAND TestYtDlpWorkerTest)

add_executable(TestEndToEnd
    src/tests/BaseTest.cpp
    src/tests/TestEndToEnd.cpp
)

target_include_directories(TestEndToEnd PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tests
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core
)

target_link_libraries(TestEndToEnd PRIVATE
    LzyAppLib
    Qt6::Test Qt6::Core Qt6::Network Qt6::Sql Qt6::Widgets
)

if(MSVC)
    target_compile_options(TestEndToEnd PRIVATE /FS)
endif()

lzy_add_qt_windows_deploy(TestEndToEnd)

add_test(NAME TestEndToEnd COMMAND TestEndToEnd)
