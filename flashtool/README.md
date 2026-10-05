# flashtool —— i.MX6ULL 烧录工具（PC 端）

Qt6 Widgets 桌面程序，是板端 `ota-agent` daemon（build 仓，常驻 :8080）的唯一
对端：加载 `board.img`（尾部自解析 manifest）→ 填板端 IP 连接体检 → 勾选分区 →
一键烧录（流式进度、断点续传、校验、A/B 翻槽）。

## 目录

```
src/            源码（transport.h 为承载抽象层；httpserver.* 为旧全拉模式
                归档，不参与构建）
3rdparty/       cpp-httplib 单头文件
resources/      exe 图标（rc 模板，仅 Windows 构建用）
CMakeLists.txt  原生与交叉共用
```

## 构建 A：Linux/WSL 原生（开发调试用）

依赖：`qt6-base-dev`（Widgets+Network）、cmake ≥ 3.16、g++。

```bash
cd tools/
cmake -B flashtool/build-native -S flashtool
cmake --build flashtool/build-native -j
./flashtool/build-native/flashtool [board.img]   # WSLg 直接出窗口
```

无头截图模式（改 UI 后出图比对）：`flashtool --screenshot out.png [镜像]`。

## 构建 B：Windows 交叉（正式交付 exe）

依赖：`mingw-w64`（x86_64 三件套 gcc/g++/windres）、Qt6 静态 MinGW 版装在
`/opt/qt6-mingw`（由 qtbase 6.4.2 源码自编，`-static`）。

toolchain 文件（历史放在易失的 /tmp，此处备份——存任意路径即可）：

```cmake
# mingw-qt-toolchain.cmake
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)
set(CMAKE_FIND_ROOT_PATH /usr/x86_64-w64-mingw32 /opt/qt6-mingw)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)
```

构建（在 tools/ 下）：

```bash
cmake -B flashtool/build-win -S flashtool \
      -DCMAKE_TOOLCHAIN_FILE=<上面文件的路径>/mingw-qt-toolchain.cmake \
      -DCMAKE_PREFIX_PATH=/opt/qt6-mingw
cmake --build flashtool/build-win -j
```

产物 `build-win/flashtool.exe`：约 41MB 全静态（`-static -static-libgcc
-static-libstdc++`，零非系统 DLL 依赖）、`-mwindows` GUI 子系统双击无黑框、
图标经 windres 链入。按部署约定拷出为 `tools/flashtool.exe`（exe 不入库）。

## 使用速记

1. 打开镜像：启动参数带路径或界面选择，manifest 自校验（CRC/magic）；
2. 填板端 IP → 连接体检（版本 + 各分区 sha 对账 + 断点提示）；
3. 勾选要烧的分区（data 为 WiFi 暂存分区自动禁选）→ 一键烧录；
4. 需要切 B 槽时烧完执行翻槽（fw_setenv slot B）。

细节与协议：`~/home_workspace/imx6ull-烧录工具-方案.md`、`…-数据交互.md`。
