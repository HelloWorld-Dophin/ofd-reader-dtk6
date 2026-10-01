# ============================================================================
# ofd-qt6-editor — OFD 阅读器+编辑器（纯 Qt6，无 DTK6 依赖）
#
# 适配：Ubuntu 24.04 / 24.10 / 26.04 等通用 Linux 发行版
# 依赖：Qt6 (Core Gui Widgets)、libdl (JNI 动态加载)
# ============================================================================
cmake_minimum_required(VERSION 3.16)
project(ofd-qt6-editor LANGUAGES CXX VERSION 1.0.3)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# ============================================================================
# Qt6 — 只需要 Core + Gui + Widgets
# ============================================================================
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)
set(QT_LIBS Qt6::Core Qt6::Gui Qt6::Widgets)

# ============================================================================
# 目标 1: ofd_jna_bridge — 静态库（C++ 封装 JNI C ABI）
# ============================================================================
add_library(ofd_jna_bridge STATIC
    src/ofd_jna_bridge.cpp
)
target_include_directories(ofd_jna_bridge PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(ofd_jna_bridge PUBLIC dl)
target_compile_options(ofd_jna_bridge PRIVATE -Wall -Wextra -O2)

# ============================================================================
# 目标 2: ofd-qt6-editor — 可执行文件
# ============================================================================
add_executable(ofd-qt6-editor
    src/main.cpp
    src/main_window.cpp
    src/property_panel.cpp
    src/ofd_render_widget.cpp
    src/about_dialog.cpp
    include/ofd_render_widget.h
    include/main_window.h
    include/property_panel.h
    include/about_dialog.h
    resources.qrc
)
target_link_libraries(ofd-qt6-editor
    PRIVATE
    ofd_jna_bridge
    ${QT_LIBS}
)
target_compile_options(ofd-qt6-editor PRIVATE -Wall -Wextra -O2)

# ============================================================================
# fonts/ 目录同步：源码 fonts/ → build/fonts/
# ============================================================================
set(FONTS_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/fonts")
set(FONTS_DST_DIR "${CMAKE_CURRENT_BINARY_DIR}/fonts")

add_custom_command(TARGET ofd-qt6-editor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${FONTS_DST_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${FONTS_SRC_DIR}" "${FONTS_DST_DIR}"
    COMMENT "同步 fonts/ → build/fonts/"
)

# ============================================================================
# 安装规则
# ============================================================================
include(GNUInstallDirs)
install(TARGETS ofd-qt6-editor RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY "${FONTS_SRC_DIR}/" DESTINATION share/ofd-qt6-editor/fonts OPTIONAL)

message(STATUS "")
message(STATUS "===== ofd-qt6-editor 构建总结 =====")
message(STATUS "  Qt 库:     ${QT_LIBS}")
message(STATUS "  C++ 标准:  ${CMAKE_CXX_STANDARD}")
message(STATUS "  无 DTK6 依赖 — 通用 Linux 发行版可运行")
message(STATUS "")
