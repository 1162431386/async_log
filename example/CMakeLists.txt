cmake_minimum_required(VERSION 3.12)
project(async_log)


message(STATUS "async_log  Compiling ....")

# 设置C++标准
set(CMAKE_CXX_STANDARD 11)

# 添加包含目录
# include_directories命令用于添加头文件的搜索路径
include_directories(
    ../include)

link_directories(../lib)


# 添加可执行文件目标
add_executable(${PROJECT_NAME}
    app_log.c
    ../src/stor_printf/common_utils.c
    ../src/stor_printf/stor_printf.c
    ../src/stor_printf/lstLib.c
    ../src/log/async_log.c
    ../src/log/ftp_printf.c
    ../src/log/packet.c
    ../src/cmd/app_cmd_server.c
    ../src/cmd/app_cmd_server.c
    ../src/cmd/app_sys_cmd.c
)

# 添加依赖的库
target_link_libraries(${PROJECT_NAME}
        pthread)




# 添加编译选项
# target_compile_options命令用于添加编译选项
# 例如：target_compile_options(YourExecutableName PRIVATE -Wall -Wextra)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)

