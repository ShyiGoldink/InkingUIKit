cmake_minimum_required(VERSION 3.20)

project(inking_minimal LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# <SDK> = 解压出来的 inking-0.5.2-win64 目录。
#
#   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
#         -DCMAKE_PREFIX_PATH="<SDK>;C:/msys64/ucrt64"
#
# 第二个路径是 MSYS2 UCRT64：`find_package(inking)` 会继续找 SDL3 与 FFmpeg
# 的开发包（这是 MinGW ABI 的硬前提，SDK 自己带不了编译器）。
find_package(inking CONFIG REQUIRED)

add_executable(hello main.cpp)
target_link_libraries(hello PRIVATE inking::inking)

# 把 SDK 里的运行期 DLL（libinking + SDL3 及传递依赖）拷到 exe 旁边。
# 不拷的表现是**静默退出**（0xC0000135），没有报错窗口。
file(GLOB INKING_RUNTIME_DLLS "${INKING_SDK_DIR}/bin/*.dll")
if(INKING_RUNTIME_DLLS)
    add_custom_command(TARGET hello POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${INKING_RUNTIME_DLLS} "$<TARGET_FILE_DIR:hello>"
        COMMENT "拷贝 Inking 运行期 DLL")
endif()
