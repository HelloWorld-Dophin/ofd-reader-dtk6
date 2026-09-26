# ============================================================================
# ofd-jna-core-native — JNI C ABI 包装层
#
# 功能：生成 libofd-jna-core.so，导出 13 个 C ABI 函数，
#       内部通过 JNI 调用 Java 端 OFDFfi.java 静态方法。
#
# 依赖：JDK (JNI headers) + ofd-jna-core (Java jar)
# ============================================================================
cmake_minimum_required(VERSION 3.16)
project(ofd-jna-core-native LANGUAGES C)

set(CMAKE_C_STANDARD 11)

# ============================================================================
# JNI 查找
# ============================================================================
find_package(JNI REQUIRED)
message(STATUS "JNI include dirs: ${JNI_INCLUDE_DIRS}")
message(STATUS "JNI libraries:    ${JNI_LIBRARIES}")

# ============================================================================
# 目标
# ============================================================================
add_library(ofd-jna-core SHARED
    src/ofd_jni_wrapper.c
)
target_include_directories(ofd-jna-core PRIVATE ${JNI_INCLUDE_DIRS})
target_link_libraries(ofd-jna-core PRIVATE ${JNI_LIBRARIES} pthread)
target_compile_options(ofd-jna-core PRIVATE -Wall -Wextra -fPIC -O2)

# 输出名：libofd-jna-core.so
set_target_properties(ofd-jna-core PROPERTIES
    OUTPUT_NAME "ofd-jna-core"
    PREFIX "lib"
)

# ============================================================================
# 运行时设置
# ============================================================================
message(STATUS "")
message(STATUS "===== 编译 libofd-jna-core.so =====")
message(STATUS "  1. 确保 ofd-jna-core Java 侧已编译:")
message(STATUS "     cd ofd-jna-core && mvn clean compile -o")
message(STATUS "")
message(STATUS "  2. 设置 OFD_JNA_CLASSPATH 环境变量 (运行时):")
message(STATUS "     export OFD_JNA_CLASSPATH=/path/to/ofd-jna-core/target/classes:\\$HOME/.m2/repository/com/sun/jna/...")
message(STATUS "")
message(STATUS "  3. 拷贝 .so 到系统库目录:")
message(STATUS "     sudo cp libofd-jna-core.so /usr/lib/")
message(STATUS "  或设置 LD_LIBRARY_PATH:")
message(STATUS "     export LD_LIBRARY_PATH=\$PWD:\$LD_LIBRARY_PATH")
message(STATUS "")
