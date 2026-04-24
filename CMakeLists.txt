cmake_minimum_required(VERSION 3.10)
project(TinyRedis LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 查找系统安装的 GTest
find_package(GTest REQUIRED)

# 添加可执行文件
add_executable(tinyredis
    main.cpp
    src/core/sds.cpp
    src/core/dict.cpp
    src/object/redisObject.cpp
    src/protocol/respParser.cpp
    src/protocol/respEncoder.cpp
    src/command/inMemoryDB.cpp
    src/command/commandParser.cpp
    src/command/commandDispatcher.cpp
    src/config/serverConfig.cpp
    src/net/socketUtil.cpp
    src/persistentence/aof.cpp
    src/net/epollServer.cpp
)

# 包含头文件目录
target_include_directories(tinyredis PRIVATE ${CMAKE_SOURCE_DIR}/include)

# SDS 测试
add_executable(test_sds
    test/test_sds.cpp
    src/core/sds.cpp
)

target_link_libraries(test_sds
    GTest::gtest
    GTest::gtest_main
)

target_include_directories(test_sds PRIVATE ${CMAKE_SOURCE_DIR}/include)

# DICT 测试
add_executable(test_dict
    test/test_dict.cpp
    src/core/sds.cpp
    src/core/dict.cpp
)

target_link_libraries(test_dict
    GTest::gtest
    GTest::gtest_main
)

target_include_directories(test_dict PRIVATE ${CMAKE_SOURCE_DIR}/include)

# 启用测试
enable_testing()
add_test(NAME SDSTests COMMAND test_sds)
add_test(NAME DictTests COMMAND test_dict)

# RESP 测试
add_executable(test_resp
    test/test_resp.cpp
    src/protocol/respParser.cpp
    src/protocol/respEncoder.cpp
)

target_link_libraries(test_resp
    GTest::gtest
    GTest::gtest_main
)

target_include_directories(test_resp PRIVATE ${CMAKE_SOURCE_DIR}/include)

add_test(NAME RespTests COMMAND test_resp)

# Config 测试
add_executable(test_config
    test/test_config.cpp
    src/config/serverConfig.cpp
)

target_link_libraries(test_config
    GTest::gtest
    GTest::gtest_main
)

target_include_directories(test_config PRIVATE ${CMAKE_SOURCE_DIR}/include)

add_test(NAME ConfigTests COMMAND test_config)

# Command 测试
add_executable(test_command
    test/test_command.cpp
    src/core/sds.cpp
    src/core/dict.cpp
    src/object/redisObject.cpp
    src/protocol/respParser.cpp
    src/protocol/respEncoder.cpp
    src/command/inMemoryDB.cpp
    src/command/commandParser.cpp
    src/command/commandDispatcher.cpp
    src/persistentence/aof.cpp
)

target_link_libraries(test_command
    GTest::gtest
    GTest::gtest_main
)

target_include_directories(test_command PRIVATE ${CMAKE_SOURCE_DIR}/include)

add_test(NAME CommandTests COMMAND test_command)

# AOF 测试
add_executable(test_aof
    test/test_aof.cpp
    src/core/sds.cpp
    src/core/dict.cpp
    src/object/redisObject.cpp
    src/protocol/respParser.cpp
    src/protocol/respEncoder.cpp
    src/command/inMemoryDB.cpp
    src/command/commandParser.cpp
    src/command/commandDispatcher.cpp
    src/persistentence/aof.cpp
)

target_link_libraries(test_aof
    GTest::gtest
    GTest::gtest_main
)

target_include_directories(test_aof PRIVATE ${CMAKE_SOURCE_DIR}/include)

add_test(NAME AOFTests COMMAND test_aof)

# E2E 测试
add_executable(test_e2e
    test/test_e2e.cpp
    src/protocol/respParser.cpp
)

target_link_libraries(test_e2e
    GTest::gtest
    GTest::gtest_main
)

target_include_directories(test_e2e PRIVATE ${CMAKE_SOURCE_DIR}/include)

add_test(NAME E2ETests COMMAND test_e2e)
