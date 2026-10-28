cmake_minimum_required(VERSION 3.16)

project(DeviceSimulator LANGUAGES CXX)

find_package(Qt6 6.5 REQUIRED COMPONENTS Quick)
qt_standard_project_setup(REQUIRES 6.5)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../.. qt-mqtt-ext)

qt_add_executable(DeviceSimulator main.cpp)
qt_add_qml_module(DeviceSimulator
    URI DeviceSim
    VERSION 1.0
    QML_FILES Main.qml
    SOURCES DeviceSimulator.h DeviceSimulator.cpp
)

set_target_properties(DeviceSimulator PROPERTIES WIN32_EXECUTABLE TRUE)
target_include_directories(DeviceSimulator PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../common)
target_compile_definitions(DeviceSimulator PRIVATE
    SIMULATOR_SECRETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../broker/.secrets")
target_link_libraries(DeviceSimulator PRIVATE Qt6::Quick MqttExt::MqttExt)
mqtt_ext_copy_runtime_dlls(DeviceSimulator)
