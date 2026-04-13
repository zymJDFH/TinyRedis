# CMake generated Testfile for 
# Source directory: /home/zym/Share/TinyRedis
# Build directory: /home/zym/Share/TinyRedis/build
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(SDSTests "/home/zym/Share/TinyRedis/build/test_sds")
set_tests_properties(SDSTests PROPERTIES  _BACKTRACE_TRIPLES "/home/zym/Share/TinyRedis/CMakeLists.txt;55;add_test;/home/zym/Share/TinyRedis/CMakeLists.txt;0;")
add_test(DictTests "/home/zym/Share/TinyRedis/build/test_dict")
set_tests_properties(DictTests PROPERTIES  _BACKTRACE_TRIPLES "/home/zym/Share/TinyRedis/CMakeLists.txt;56;add_test;/home/zym/Share/TinyRedis/CMakeLists.txt;0;")
add_test(RespTests "/home/zym/Share/TinyRedis/build/test_resp")
set_tests_properties(RespTests PROPERTIES  _BACKTRACE_TRIPLES "/home/zym/Share/TinyRedis/CMakeLists.txt;72;add_test;/home/zym/Share/TinyRedis/CMakeLists.txt;0;")
add_test(CommandTests "/home/zym/Share/TinyRedis/build/test_command")
set_tests_properties(CommandTests PROPERTIES  _BACKTRACE_TRIPLES "/home/zym/Share/TinyRedis/CMakeLists.txt;91;add_test;/home/zym/Share/TinyRedis/CMakeLists.txt;0;")
