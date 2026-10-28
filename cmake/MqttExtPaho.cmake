include_guard(GLOBAL)

include(FetchContent)

if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

set(PAHO_WITH_SSL ${MQTT_EXT_WITH_TLS} CACHE BOOL "" FORCE)
set(PAHO_WITH_LIBRESSL OFF CACHE BOOL "" FORCE)
set(PAHO_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(PAHO_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(PAHO_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
set(PAHO_BUILD_DOCUMENTATION OFF CACHE BOOL "" FORCE)
set(PAHO_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(PAHO_ENABLE_CPACK OFF CACHE BOOL "" FORCE)
set(PAHO_HIGH_PERFORMANCE ON CACHE BOOL "" FORCE)

set(_mqtt_ext_paho_source
    URL https://github.com/eclipse-paho/paho.mqtt.c/archive/refs/tags/v1.3.16.tar.gz
    URL_HASH SHA256=8b960f51edc7e03507637d987882bc486d8f4be6e79431bf99e2763344fd14c5
)
if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.28)
    list(APPEND _mqtt_ext_paho_source EXCLUDE_FROM_ALL)
endif()

FetchContent_Declare(paho_mqtt_c ${_mqtt_ext_paho_source})
FetchContent_MakeAvailable(paho_mqtt_c)

if(MQTT_EXT_WITH_TLS)
    set(MQTT_EXT_PAHO_TARGET eclipse-paho-mqtt-c::paho-mqtt3as-static)
else()
    set(MQTT_EXT_PAHO_TARGET eclipse-paho-mqtt-c::paho-mqtt3a-static)
endif()
