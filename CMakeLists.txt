cmake_minimum_required(VERSION 3.16)

project(MqttExt VERSION 1.0.0 LANGUAGES C CXX)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(MQTT_EXT_TOP_LEVEL ON)
else()
    set(MQTT_EXT_TOP_LEVEL OFF)
endif()

option(MQTT_EXT_WITH_TLS "Build with TLS support (ssl://, mqtts://, wss://) through OpenSSL" ON)
option(MQTT_EXT_BUILD_TESTS "Build the MqttExt tests" ${MQTT_EXT_TOP_LEVEL})

find_package(Qt6 6.4 REQUIRED COMPONENTS Core)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(MQTT_EXT_WITH_TLS)
    include(cmake/MqttExtOpenSsl.cmake)
else()
    set(MQTT_EXT_OPENSSL_RUNTIME_DLLS "" CACHE INTERNAL "")
endif()
include(cmake/MqttExtPaho.cmake)

function(mqtt_ext_copy_runtime_dlls target)
    if(MQTT_EXT_OPENSSL_RUNTIME_DLLS)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${MQTT_EXT_OPENSSL_RUNTIME_DLLS} $<TARGET_FILE_DIR:${target}>
            VERBATIM)
    endif()
endfunction()

add_library(MqttExt STATIC
    include/MqttExt/Client.h
    include/MqttExt/ConnectOptions.h
    include/MqttExt/Message.h
    include/MqttExt/Topic.h
    src/CallbackRegistry.cpp
    src/CallbackRegistry.h
    src/Client.cpp
    src/Topic.cpp
)
add_library(MqttExt::MqttExt ALIAS MqttExt)

set_target_properties(MqttExt PROPERTIES AUTOMOC ON)
target_compile_features(MqttExt PUBLIC cxx_std_17)
target_include_directories(MqttExt
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(MqttExt
    PUBLIC Qt6::Core
    PRIVATE ${MQTT_EXT_PAHO_TARGET}
)
if(MQTT_EXT_WITH_TLS)
    target_compile_definitions(MqttExt PRIVATE MQTT_EXT_WITH_TLS)
endif()

if(MQTT_EXT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
