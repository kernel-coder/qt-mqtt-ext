find_package(Qt6 REQUIRED COMPONENTS Network Test)

function(mqtt_ext_add_test name)
    add_executable(${name} ${name}.cpp)
    set_target_properties(${name} PROPERTIES AUTOMOC ON)
    target_link_libraries(${name} PRIVATE MqttExt::MqttExt Qt6::Network Qt6::Test)
    target_compile_definitions(${name} PRIVATE MQTT_EXT_SECRETS_DIR="${PROJECT_SOURCE_DIR}/broker/.secrets")
    mqtt_ext_copy_runtime_dlls(${name})
    add_test(NAME ${name} COMMAND ${name})
    if(WIN32 AND CMAKE_VERSION VERSION_GREATER_EQUAL 3.22)
        set_tests_properties(${name} PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
    endif()
endfunction()

mqtt_ext_add_test(TopicTest)
mqtt_ext_add_test(ClientTest)
