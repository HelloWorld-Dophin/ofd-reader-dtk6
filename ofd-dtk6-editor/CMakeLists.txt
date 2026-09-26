# ============================================================================
# ofd-dtk6-editor — OFD 阅读器+编辑器（DTK6 / Deepin Tool Kit 6）
# ============================================================================
cmake_minimum_required(VERSION 3.16)
project(ofd-dtk6-editor LANGUAGES CXX VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)    # Qt MOC
set(CMAKE_AUTORCC ON)    # Qt RCC
set(CMAKE_AUTOUIC ON)    # Qt UIC

# ============================================================================
# Qt6 查找（DTK6 基于 Qt6）
# ============================================================================
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)
set(QT_LIBS Qt6::Core Qt6::Gui Qt6::Widgets)

# ============================================================================
# Deepin DTK6 — 手动链接（Deepin/UOS 上无 CMake package）
# ============================================================================
# 实际安装位置（通过 dpkg -L libdtk6core-dev 确认）：
#   头文件: /usr/include/dtk6/
#   库文件: /usr/lib/x86_64-linux-gnu/libdtk6core.so / libdtk6gui.so / libdtk6widget.so
set(DTK6_INCLUDE_DIRS "/usr/include/dtk6")
find_library(DTK6_CORE_LIB  NAMES dtk6core  PATHS /usr/lib/x86_64-linux-gnu)
find_library(DTK6_GUI_LIB   NAMES dtk6gui   PATHS /usr/lib/x86_64-linux-gnu)
find_library(DTK6_WIDGET_LIB NAMES dtk6widget PATHS /usr/lib/x86_64-linux-gnu)

if (NOT DTK6_CORE_LIB OR NOT DTK6_GUI_LIB OR NOT DTK6_WIDGET_LIB)
    message(FATAL_ERROR
        "找不到 DTK6 库！已安装的包:\n"
        "  sudo apt install libdtk6core-dev libdtk6gui-dev libdtk6widget-dev\n"
        "当前查找结果:\n"
        "  dtk6core:  ${DTK6_CORE_LIB}\n"
        "  dtk6gui:   ${DTK6_GUI_LIB}\n"
        "  dtk6widget: ${DTK6_WIDGET_LIB}")
endif()

message(STATUS "DTK6 库: ${DTK6_CORE_LIB} ${DTK6_GUI_LIB} ${DTK6_WIDGET_LIB}")
message(STATUS "DTK6 头: ${DTK6_INCLUDE_DIRS}")

# ============================================================================
# 目标 1: ofd_jna_bridge — 静态库（C++ 封装）
# ============================================================================
add_library(ofd_jna_bridge STATIC
    src/ofd_jna_bridge.cpp
)
target_include_directories(ofd_jna_bridge PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(ofd_jna_bridge PUBLIC dl)  # dlopen/dlsym
target_compile_options(ofd_jna_bridge PRIVATE
    -Wall -Wextra -Wpedantic -O2
)

# ============================================================================
# 目标 2: ofd-dtk6-editor — 可执行文件
# ============================================================================
add_executable(ofd-dtk6-editor
    src/main.cpp
    src/main_window.cpp
    src/property_panel.cpp
    src/ofd_render_widget.cpp
    include/ofd_render_widget.h
    include/main_window.h
    include/property_panel.h
    resources.qrc
)
target_include_directories(ofd-dtk6-editor PRIVATE
    ${DTK6_INCLUDE_DIRS}
    ${DTK6_INCLUDE_DIRS}/DCore
    ${DTK6_INCLUDE_DIRS}/DGui
    ${DTK6_INCLUDE_DIRS}/DWidget
)
target_link_libraries(ofd-dtk6-editor
    PRIVATE
    ofd_jna_bridge
    ${QT_LIBS}
    ${DTK6_CORE_LIB}
    ${DTK6_GUI_LIB}
    ${DTK6_WIDGET_LIB}
)
target_compile_options(ofd-dtk6-editor PRIVATE
    -Wall -Wextra -Wpedantic -O2
)

# ============================================================================
# fonts/ 目录同步：源码 fonts/ → build/fonts/
# C++ 侧用 QCoreApplication::applicationDirPath() + "/fonts" 加载
# ============================================================================
set(FONTS_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/fonts")
set(FONTS_DST_DIR "${CMAKE_CURRENT_BINARY_DIR}/fonts")

add_custom_command(TARGET ofd-dtk6-editor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${FONTS_DST_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${FONTS_SRC_DIR}" "${FONTS_DST_DIR}"
    COMMENT "同步 fonts/ → build/fonts/"
)

# install 时也带上 fonts
install(DIRECTORY "${FONTS_SRC_DIR}/" DESTINATION share/ofd-dtk6-editor/fonts OPTIONAL)

# ============================================================================
# 目标 3: test-render — 离屏渲染验证
# ============================================================================
add_executable(test-render
    src/test_render.cpp
    src/ofd_render_widget.cpp
    include/ofd_render_widget.h
)
target_include_directories(test-render PRIVATE
    ${DTK6_INCLUDE_DIRS}
    ${DTK6_INCLUDE_DIRS}/DCore
    ${DTK6_INCLUDE_DIRS}/DGui
    ${DTK6_INCLUDE_DIRS}/DWidget
)
target_link_libraries(test-render
    PRIVATE
    ofd_jna_bridge
    ${QT_LIBS}
    ${DTK6_CORE_LIB}
    ${DTK6_GUI_LIB}
    ${DTK6_WIDGET_LIB}
)
target_compile_options(test-render PRIVATE
    -Wall -Wextra -Wpedantic -O2
)

# ============================================================================
# 安装规则
# ============================================================================
include(GNUInstallDirs)
install(TARGETS ofd-dtk6-editor RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(TARGETS ofd_jna_bridge ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})

message(STATUS "")
message(STATUS "===== 构建总结 =====")
message(STATUS "  Qt 库:     ${QT_LIBS}")
message(STATUS "  DTK6 库:   ${DTK6_CORE_LIB} ${DTK6_GUI_LIB} ${DTK6_WIDGET_LIB}")
message(STATUS "  C++ 标准:  ${CMAKE_CXX_STANDARD}")
message(STATUS "")
