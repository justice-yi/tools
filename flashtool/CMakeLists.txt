cmake_minimum_required(VERSION 3.16)
project(flashtool LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# 原生(WSL/Linux)开发与 mingw 交叉(出 Windows exe)共用此文件；
# 交叉时 -DCMAKE_TOOLCHAIN_FILE=<mingw-toolchain.cmake>
find_package(Qt6 REQUIRED COMPONENTS Widgets Network)

add_executable(flashtool
	src/main.cpp
	src/mainwindow.cpp src/mainwindow.h
	src/frameless.cpp  src/frameless.h
	src/manifest.cpp   src/manifest.h
	src/transport.h
	# 服务模式：PC 为 HTTP 客户端，板端 ota-agent daemon 监听
	src/httpclient.cpp src/httpclient.h
	# httpserver.{h,cpp}（旧全拉模式）已退役，仅存档不参与构建
	)

target_include_directories(flashtool PRIVATE src 3rdparty)
target_link_libraries(flashtool PRIVATE Qt6::Widgets Qt6::Network)

if(WIN32)
	# exe 图标：rc 资源 windres 编译后链入（见 resources/flashtool.rc.in 注释）
	configure_file(resources/flashtool.rc.in ${CMAKE_CURRENT_BINARY_DIR}/flashtool.rc @ONLY)
	target_sources(flashtool PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/flashtool.rc)
	target_link_libraries(flashtool PRIVATE ws2_32)
	# 静态链 MinGW 运行时(libgcc/libstdc++/winpthread)，exe 不再依赖任何非系统 DLL
	# -mwindows: GUI 子系统，双击不弹黑框（日志走界面日志面板，不需要控制台）
	target_link_options(flashtool PRIVATE -static -static-libgcc -static-libstdc++ -mwindows)
endif()
