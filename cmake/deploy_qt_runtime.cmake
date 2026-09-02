if(NOT DEFINED WINDEPLOYQT_EXECUTABLE OR NOT DEFINED TARGET_FILE)
    message(FATAL_ERROR "WINDEPLOYQT_EXECUTABLE and TARGET_FILE are required.")
endif()

set(_arguments --no-translations --no-compiler-runtime)
if(DEFINED DEPLOY_MODE AND NOT DEPLOY_MODE STREQUAL "")
    list(APPEND _arguments "${DEPLOY_MODE}")
endif()
list(APPEND _arguments "${TARGET_FILE}")

execute_process(
    COMMAND "${WINDEPLOYQT_EXECUTABLE}" ${_arguments}
    RESULT_VARIABLE _result
    OUTPUT_VARIABLE _output
    ERROR_VARIABLE _error
)
if(_output)
    message(STATUS "${_output}")
endif()
if(_result)
    message(WARNING "windeployqt failed with exit code ${_result}; continuing with explicit Qt runtime deployment. ${_error}")
elseif(_error)
    message(STATUS "${_error}")
endif()

# Keep local Windows test builds runnable when windeployqt cannot query
# qtpaths. The explicit copy is also useful for Qt installations that omit
# transitive modules from windeployqt's dependency scan.
if(DEFINED QT_PREFIX AND EXISTS "${QT_PREFIX}")
    file(GLOB _qt_runtime_dlls "${QT_PREFIX}/bin/Qt6*.dll")
    if(_qt_runtime_dlls)
        file(COPY ${_qt_runtime_dlls} DESTINATION "${TARGET_FILE_DIR}")
    endif()
    foreach(_plugin_dir platforms generic imageformats iconengines networkinformation sqldrivers styles tls)
        if(EXISTS "${QT_PREFIX}/plugins/${_plugin_dir}")
            file(COPY "${QT_PREFIX}/plugins/${_plugin_dir}/" DESTINATION "${TARGET_FILE_DIR}")
        endif()
    endforeach()
endif()
