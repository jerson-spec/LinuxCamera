cmake_minimum_required(VERSION 3.16)
project(camera VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Debug/Release/RelWithDebInfo" FORCE)
endif()

# ---------- 第三方 inih ----------
add_library(inih STATIC
    third_party/inih/ini.c
)
target_include_directories(inih PUBLIC
    third_party/inih
)

# ---------- 自己的核心库 ----------
add_library(camera_core STATIC
    src/config.c
    src/log.c
)
target_include_directories(camera_core PUBLIC
    include
)
target_compile_options(camera_core PRIVATE
    -Wall -Wextra
)
target_compile_definitions(camera_core PUBLIC
    _POSIX_C_SOURCE=200809L
)
target_link_libraries(camera_core PRIVATE
    inih
)

# ---------- 正式程序 ----------
add_executable(camera
    src/main.c
)
target_compile_options(camera PRIVATE
    -Wall -Wextra
)
target_link_libraries(camera PRIVATE
    camera_core
)

# ---------- V4L2 测试 ----------
add_executable(test_Get_images
    V4L2/test_Get_images.c
)
target_compile_options(test_Get_images PRIVATE
    -Wall -Wextra
)

target_link_libraries(test_Get_images PRIVATE
    camera_core
)

# --------- lcd 测试 -----------
add_executable(test_Draw_region
    LCD/test_Draw_region.c
)
target_compile_options(test_Draw_region PRIVATE
    -Wall -Wextra
)

target_link_libraries(test_Draw_region PRIVATE
    camera_core
)


# ---------- 配置模块的独立测试 ----------
add_executable(config_test
    tests/config_test.c
)
target_link_libraries(config_test PRIVATE
    camera_core
)

# ---------- 安装 ----------
install(TARGETS camera
    RUNTIME DESTINATION bin
)
install(FILES config/camera.ini
    DESTINATION etc/camera
)
install(FILES config/lcd.ini
	DESTINATION etc/lcd
)