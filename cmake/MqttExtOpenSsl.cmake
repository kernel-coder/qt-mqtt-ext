include_guard(GLOBAL)

if(ANDROID)
    set(_mqtt_ext_abi "${CMAKE_ANDROID_ARCH_ABI}")
    if(NOT _mqtt_ext_abi)
        set(_mqtt_ext_abi "${ANDROID_ABI}")
    endif()

    set(MQTT_EXT_ANDROID_OPENSSL_DIR "${ANDROID_SDK_ROOT}/android_openssl/ssl_3"
        CACHE PATH "KDAB android_openssl ssl_3 directory that provides OpenSSL for Android builds")

    if(NOT OPENSSL_SSL_LIBRARY AND EXISTS "${MQTT_EXT_ANDROID_OPENSSL_DIR}/include/openssl/ssl.h")
        set(OPENSSL_INCLUDE_DIR "${MQTT_EXT_ANDROID_OPENSSL_DIR}/include" CACHE PATH "" FORCE)
        set(OPENSSL_SSL_LIBRARY "${MQTT_EXT_ANDROID_OPENSSL_DIR}/${_mqtt_ext_abi}/libssl_3.so" CACHE FILEPATH "" FORCE)
        set(OPENSSL_CRYPTO_LIBRARY "${MQTT_EXT_ANDROID_OPENSSL_DIR}/${_mqtt_ext_abi}/libcrypto_3.so" CACHE FILEPATH "" FORCE)
    endif()
elseif(MINGW AND NOT OPENSSL_ROOT_DIR)
    get_filename_component(_mqtt_ext_compiler_dir "${CMAKE_C_COMPILER}" DIRECTORY)
    get_filename_component(_mqtt_ext_mingw_opt "${_mqtt_ext_compiler_dir}/../opt" ABSOLUTE)
    if(EXISTS "${_mqtt_ext_mingw_opt}/include/openssl/ssl.h")
        set(OPENSSL_ROOT_DIR "${_mqtt_ext_mingw_opt}")
    endif()
elseif(MSVC AND NOT OPENSSL_ROOT_DIR AND Qt6_DIR)
    get_filename_component(_mqtt_ext_qt_openssl "${Qt6_DIR}/../../../../../Tools/OpenSSLv3/Win_x64" ABSOLUTE)
    if(EXISTS "${_mqtt_ext_qt_openssl}/include/openssl/ssl.h")
        set(OPENSSL_ROOT_DIR "${_mqtt_ext_qt_openssl}")
    endif()
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin" AND NOT OPENSSL_ROOT_DIR)
    find_program(_mqtt_ext_brew brew)
    if(_mqtt_ext_brew)
        execute_process(COMMAND "${_mqtt_ext_brew}" --prefix openssl@3
            OUTPUT_VARIABLE _mqtt_ext_brew_openssl
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET)
        if(EXISTS "${_mqtt_ext_brew_openssl}/include/openssl/ssl.h")
            set(OPENSSL_ROOT_DIR "${_mqtt_ext_brew_openssl}")
        endif()
    endif()
endif()

find_package(OpenSSL)
if(NOT OPENSSL_FOUND)
    message(FATAL_ERROR
        "MqttExt: OpenSSL was not found. Point FindOpenSSL at it (OPENSSL_ROOT_DIR, or "
        "OPENSSL_INCLUDE_DIR + OPENSSL_SSL_LIBRARY + OPENSSL_CRYPTO_LIBRARY), or configure with "
        "-DMQTT_EXT_WITH_TLS=OFF to build without ssl://, mqtts:// and wss:// support.")
endif()
message(STATUS "MqttExt: TLS through OpenSSL ${OPENSSL_VERSION} (${OPENSSL_SSL_LIBRARY})")

set(_mqtt_ext_openssl_dlls "")
if(WIN32 AND OPENSSL_SSL_LIBRARY MATCHES "\\.dll\\.a$|\\.lib$")
    get_filename_component(_mqtt_ext_openssl_root "${OPENSSL_INCLUDE_DIR}/.." ABSOLUTE)
    file(GLOB _mqtt_ext_openssl_dlls
        "${_mqtt_ext_openssl_root}/bin/libcrypto-*.dll"
        "${_mqtt_ext_openssl_root}/bin/libssl-*.dll")
    if(NOT _mqtt_ext_openssl_dlls)
        message(WARNING "MqttExt: OpenSSL is linked dynamically but no DLLs were found in ${_mqtt_ext_openssl_root}/bin")
    endif()
endif()
set(MQTT_EXT_OPENSSL_RUNTIME_DLLS "${_mqtt_ext_openssl_dlls}" CACHE INTERNAL "OpenSSL DLLs an executable linking MqttExt needs at runtime")
