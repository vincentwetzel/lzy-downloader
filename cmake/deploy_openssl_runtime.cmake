if(NOT DEFINED OPENSSL_RUNTIME_DIR OR OPENSSL_RUNTIME_DIR STREQUAL "")
    message(WARNING "OPENSSL_RUNTIME_DIR was not provided; skipping OpenSSL runtime deployment.")
    return()
endif()

if(NOT DEFINED TARGET_RUNTIME_DIR OR TARGET_RUNTIME_DIR STREQUAL "")
    message(FATAL_ERROR "TARGET_RUNTIME_DIR was not provided for OpenSSL runtime deployment.")
endif()

file(GLOB _openssl_runtime_dlls
    "${OPENSSL_RUNTIME_DIR}/libcrypto*.dll"
    "${OPENSSL_RUNTIME_DIR}/libssl*.dll"
)

if(NOT _openssl_runtime_dlls)
    message(WARNING "No OpenSSL runtime DLLs were found in ${OPENSSL_RUNTIME_DIR}; Qt HTTPS requests may fail if qopensslbackend is deployed.")
    return()
endif()

file(COPY ${_openssl_runtime_dlls} DESTINATION "${TARGET_RUNTIME_DIR}")
