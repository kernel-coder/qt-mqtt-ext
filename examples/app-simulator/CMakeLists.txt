cmake_minimum_required(VERSION 3.16)

project(AppSimulator LANGUAGES CXX)

find_package(Qt6 6.5 REQUIRED COMPONENTS Quick)
qt_standard_project_setup(REQUIRES 6.5)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../.. qt-mqtt-ext)

qt_add_executable(AppSimulator main.cpp)
qt_add_qml_module(AppSimulator
    URI AppSim
    VERSION 1.0
    QML_FILES Main.qml
    SOURCES AppMonitor.h AppMonitor.cpp
)

set_target_properties(AppSimulator PROPERTIES WIN32_EXECUTABLE TRUE)
target_include_directories(AppSimulator PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../common)
target_compile_definitions(AppSimulator PRIVATE
    SIMULATOR_SECRETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../broker/.secrets")
target_link_libraries(AppSimulator PRIVATE Qt6::Quick MqttExt::MqttExt)
mqtt_ext_copy_runtime_dlls(AppSimulator)
