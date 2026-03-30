# QK4 Unit Tests
# Uses Qt Test framework for test infrastructure

# RadioState CAT command parser tests
add_executable(test_radiostate test_radiostate.cpp
    ${CMAKE_SOURCE_DIR}/src/models/radiostate.cpp
)
target_include_directories(test_radiostate PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_radiostate Qt6::Core Qt6::Test)
add_test(NAME RadioStateTests COMMAND test_radiostate)

# RadioUtils shared utility tests
add_executable(test_radioutils test_radioutils.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/radioutils.cpp
)
target_include_directories(test_radioutils PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_radioutils Qt6::Core Qt6::Test)
add_test(NAME RadioUtilsTests COMMAND test_radioutils)
