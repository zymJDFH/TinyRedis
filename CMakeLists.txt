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
    src/protocol/respParser.cpp
    src/protocol/respEncoder.cpp
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
