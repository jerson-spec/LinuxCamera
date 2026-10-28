set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

# 1. SDK 根目录
set(SDK_ROOT "/home/book/100ask_imx6ull-sdk/ToolChain/arm-buildroot-linux-gnueabihf_sdk-buildroot")

# 2. 交叉编译器：必须指向 SDK 里的 gcc，不是 /usr/bin/gcc
set(CMAKE_C_COMPILER
    "${SDK_ROOT}/bin/arm-buildroot-linux-gnueabihf-gcc"
)

# 3. 目标 sysroot：板子 rootfs 对应的头文件和库
set(CMAKE_SYSROOT
    "${SDK_ROOT}/arm-buildroot-linux-gnueabihf/sysroot"
)

# 4. 查找规则：库/头文件只在 sysroot 里找，主机工具在主机找
set(CMAKE_FIND_ROOT_PATH "${CMAKE_SYSROOT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)