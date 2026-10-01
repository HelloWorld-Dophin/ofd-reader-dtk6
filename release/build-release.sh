#!/bin/bash
# ============================================================================
# build-release.sh — 一键构建发布包（含内置 JRE，无需用户预装 Java）
# 目标: ofd-qt6-editor 纯 Qt6 版（Ubuntu 24.04/24.10/26.04 通用）
#
# 产物:
#   release/out/ofd-editor_1.0.3-qt6_amd64.deb
# ============================================================================

set -e

ARCH="amd64"
VERSION="1.0.3-qt6"

echo "========================================"
echo "OFD Editor (Qt6) Release Builder"
echo "  版本: $VERSION"
echo "  目标架构: $ARCH"
echo "========================================"

# --- 路径 ---
BASE="$(cd "$(dirname "$0")/.." && pwd)"
RELEASE_DIR="$BASE/release"
OUT_DIR="$RELEASE_DIR/out/ofd-editor"

# JDK 路径
JAVA17_HOME="${JAVA17_HOME:-/usr/lib/jvm/java-17-openjdk-amd64}"
JLINK="$JAVA17_HOME/bin/jlink"

# ============================================================================
# Step 0: 清理旧产物
# ============================================================================
echo ""
echo "=== Step 0: 清理 ==="
rm -rf "$OUT_DIR"
rm -rf "$RELEASE_DIR/jre"
rm -rf "$RELEASE_DIR/libs"
rm -rf "$RELEASE_DIR/deb-staging"

# ============================================================================
# Step 1: 编译
# ============================================================================
echo ""
echo "=== Step 1: 编译 ==="

# --- Java ---
cd "$BASE/ofd-jna-core"
mvn clean compile -o 2>&1 | tail -3

# --- 原生 C/C++ ---
JNI_BUILD="$BASE/ofd-jna-core-native/build"
EDITOR_BUILD="$BASE/ofd-qt6-editor/build"

rm -rf "$JNI_BUILD" "$EDITOR_BUILD"

# JNI C 层
mkdir -p "$JNI_BUILD"
cd "$JNI_BUILD"
echo "--- JNI C 层 ---"
cmake -DCMAKE_BUILD_TYPE=Release "$BASE/ofd-jna-core-native" 2>&1 | tail -5
cmake --build . -j4 2>&1 | tail -5

# Qt6 主程序
mkdir -p "$EDITOR_BUILD"
cd "$EDITOR_BUILD"
echo "--- Qt6 主程序 ---"
cmake -DCMAKE_BUILD_TYPE=Release "$BASE/ofd-qt6-editor" 2>&1 | tail -10
cmake --build . -j4 2>&1 | tail -5

# ============================================================================
# Step 2: jlink 裁剪内置 JRE
# ============================================================================
echo ""
echo "=== Step 2: jlink 裁剪 JRE ==="

JMODULES="java.base,java.desktop,java.xml,java.logging,java.management,java.security.sasl,java.net.http,jdk.unsupported,jdk.zipfs,jdk.crypto.ec,java.prefs,java.naming,java.scripting"

if [ ! -x "$JLINK" ]; then
    echo "❌ jlink 不存在: $JLINK"
    exit 1
fi
$JLINK \
    --module-path "$JAVA17_HOME/jmods" \
    --add-modules "$JMODULES" \
    --output "$RELEASE_DIR/jre" \
    --compress 2 \
    --no-header-files \
    --no-man-pages \
    --strip-debug
echo "  ✅ JRE: $(du -sh "$RELEASE_DIR/jre" | cut -f1)"

# ============================================================================
# Step 3: 收集依赖 jars
# ============================================================================
echo ""
echo "=== Step 3: 收集依赖 jars ==="
mkdir -p "$RELEASE_DIR/libs"

COUNT=0
while IFS= read -r jar; do
    cp "$jar" "$RELEASE_DIR/libs/"
    COUNT=$((COUNT + 1))
done < <(find "$HOME/.m2/repository" -name '*.jar' 2>/dev/null \
    | grep -v sources | grep -v javadoc | grep -v tests)
echo "  收集 jars: $COUNT（全 m2，跟 dev 运行时一致）"

# ============================================================================
# Step 4: 组装发布目录
# ============================================================================
echo ""
echo "=== Step 4: 组装发布目录 ==="

mkdir -p "$OUT_DIR/bin"
mkdir -p "$OUT_DIR/lib"
mkdir -p "$OUT_DIR/java/classes"
mkdir -p "$OUT_DIR/java/libs"
mkdir -p "$OUT_DIR/jre"
mkdir -p "$OUT_DIR/fonts"

# 二进制产物
cp "$EDITOR_BUILD/ofd-qt6-editor"   "$OUT_DIR/bin/"
cp "$JNI_BUILD/libofd-jna-core.so"  "$OUT_DIR/lib/"

# Java 编译产物
cp -r "$BASE/ofd-jna-core/target/classes"/* "$OUT_DIR/java/classes/" 2>/dev/null || true

# 依赖 jars
cp "$RELEASE_DIR/libs"/*.jar "$OUT_DIR/java/libs/" 2>/dev/null || true

# JRE
cp -r "$RELEASE_DIR/jre"/* "$OUT_DIR/jre/"

# 字体（Qt6 版自带 fonts/）
cp -r "$BASE/ofd-qt6-editor/fonts/"* "$OUT_DIR/fonts/" 2>/dev/null || true

# 启动脚本
cp "$RELEASE_DIR/ofd-editor" "$OUT_DIR/bin/"
chmod +x "$OUT_DIR/bin/ofd-editor"

# ============================================================================
# Step 5: deb 打包
# ============================================================================
echo ""
echo "=== Step 5: deb 打包 ==="

DEB_STAGING="$RELEASE_DIR/deb-staging"
rm -rf "$DEB_STAGING"
mkdir -p "$DEB_STAGING/DEBIAN"
mkdir -p "$DEB_STAGING/usr/bin"
mkdir -p "$DEB_STAGING/usr/share/applications"
mkdir -p "$DEB_STAGING/usr/share/icons/hicolor/scalable/apps"
mkdir -p "$DEB_STAGING/opt/ofd-editor"

# --- 生成 QT6 专用 control（无 DTK 依赖） ---
cat > "$DEB_STAGING/DEBIAN/control" << 'CTRL'
Package: ofd-editor
Version: PLACEHOLDER_VERSION
Section: office
Priority: optional
Architecture: amd64
Maintainer: Kelvinxi <kelvinxi@outlook.com>
Depends: libqt6core6, libqt6gui6, libqt6widgets6, libgl1, libc6 (>= 2.34)
Suggests: fonts-noto-cjk
Description: OFD 阅读器+简易编辑器（纯 Qt6 GUI + JNA FFI）
 基于 Qt6 构建的 OFD 电子文档阅读器和简易编辑器。
 支持打开、浏览、渲染 OFD 文件，支持增值税电子普通发票
 完整渲染（文字、Path 细线、表格、二维码、嵌套公章）。
 底层使用 ofdrw 2.0.2 解析，JNA 5.14 实现 FFI 桥接。
 本包内置裁剪版 JRE（jlink），用户无需预装 Java。
CTRL
sed -i "s/PLACEHOLDER_VERSION/$VERSION/" "$DEB_STAGING/DEBIAN/control"

# scripts
cp "$BASE/debian/postinst" "$DEB_STAGING/DEBIAN/"
cp "$BASE/debian/prerm"    "$DEB_STAGING/DEBIAN/"
chmod 755 "$DEB_STAGING/DEBIAN/postinst" "$DEB_STAGING/DEBIAN/prerm"

# desktop 文件
cp "$BASE/debian/ofd-editor.desktop" "$DEB_STAGING/usr/share/applications/"

# 图标
SRC_ICON="$BASE/ofd-qt6-editor/icons/app.svg"
if [ -f "$SRC_ICON" ]; then
    cp "$SRC_ICON" "$DEB_STAGING/usr/share/icons/hicolor/scalable/apps/ofd-editor.svg"
fi

# 应用文件
cp -r "$OUT_DIR"/* "$DEB_STAGING/opt/ofd-editor/"

# /usr/bin 启动入口
ln -sf "../../opt/ofd-editor/bin/ofd-editor" "$DEB_STAGING/usr/bin/ofd-editor"

# 权限
chmod 755 "$DEB_STAGING/opt/ofd-editor/bin/"*
chmod 755 "$DEB_STAGING/opt/ofd-editor/jre/bin/"* 2>/dev/null || true

# 打包
DEB_OUT="$RELEASE_DIR/out/ofd-editor_${VERSION}_${ARCH}.deb"
if command -v fakeroot >/dev/null 2>&1; then
    fakeroot dpkg-deb --build --root-owner-group "$DEB_STAGING" "$DEB_OUT"
else
    dpkg-deb --build --root-owner-group "$DEB_STAGING" "$DEB_OUT" 2>&1
fi

rm -rf "$DEB_STAGING"

# ============================================================================
# Step 6: 总结
# ============================================================================
echo ""
echo "========================================"
echo "✅ 构建完成 ($ARCH / $VERSION)"
echo "========================================"
echo "deb 包: $DEB_OUT ($(du -sh "$DEB_OUT" 2>/dev/null | cut -f1 || echo 'N/A'))"
echo "发布目录: $OUT_DIR"
echo "总大小: $(du -sh "$OUT_DIR" | cut -f1)"
echo ""
echo "目录结构:"
du -sh "$OUT_DIR"/*/ 2>/dev/null
echo ""
echo "=== 产物架构验证 ==="
file "$OUT_DIR/bin/ofd-qt6-editor"
file "$OUT_DIR/lib/libofd-jna-core.so"
if [ -x "$OUT_DIR/jre/bin/java" ]; then
    file "$OUT_DIR/jre/bin/java"
fi
