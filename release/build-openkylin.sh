#!/bin/bash
# ============================================================================
# build-openkylin.sh — OpenKylin 2.0 / 通用 Linux 发布包
# 核心差异：内置 Qt6 运行时（6.8），解决 openKylin 2.0 自带 Qt 6.5 ABI 不兼容
#
# 产物:
#   release/out/ofd-editor_1.0.4-openkylin_amd64.deb
# ============================================================================

set -e

ARCH="amd64"
VERSION="1.0.4-openkylin"

echo "========================================"
echo "OFD Editor (openKylin) Release Builder"
echo "  版本: $VERSION"
echo "  架构: $ARCH"
echo "  特点: 内置 Qt6 运行时 + 内置 JRE"
echo "========================================"

# --- 路径 ---
BASE="$(cd "$(dirname "$0")/.." && pwd)"
RELEASE_DIR="$BASE/release"
OUT_DIR="$RELEASE_DIR/out/ofd-editor"

# JDK 路径
JAVA17_HOME="${JAVA17_HOME:-/usr/lib/jvm/java-17-openjdk-amd64}"
JLINK="$JAVA17_HOME/bin/jlink"

# Qt6 系统路径
QT6_SYS_LIB="/usr/lib/x86_64-linux-gnu"
QT6_SYS_PLUGINS="$QT6_SYS_LIB/qt6/plugins"
QT6_BUILTIN_DIR="$OUT_DIR/qt6"

# ============================================================================
# Step 0: 清理
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
cmake -DCMAKE_BUILD_TYPE=Release "$BASE/ofd-jna-core-native" 2>&1 | tail -3
cmake --build . -j4 2>&1 | tail -3

# Qt6 主程序
mkdir -p "$EDITOR_BUILD"
cd "$EDITOR_BUILD"
echo "--- Qt6 主程序 ---"
cmake -DCMAKE_BUILD_TYPE=Release "$BASE/ofd-qt6-editor" 2>&1 | tail -3
cmake --build . -j4 2>&1 | tail -3

# ============================================================================
# Step 2: jlink 裁剪内置 JRE
# ============================================================================
echo ""
echo "=== Step 2: jlink 裁剪 JRE ==="

JMODULES="java.base,java.desktop,java.xml,java.logging,java.management,java.security.sasl,java.net.http,jdk.unsupported,jdk.zipfs,jdk.crypto.ec,java.prefs,java.naming,java.scripting"

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
echo "  收集 jars: $COUNT"

# ============================================================================
# Step 4: 内置 Qt6 运行时（openKylin 关键差异）
# ============================================================================
echo ""
echo "=== Step 4: 内置 Qt6 运行时 ==="
mkdir -p "$QT6_BUILTIN_DIR/lib"
mkdir -p "$QT6_BUILTIN_DIR/plugins"

# --- 4a. 复制 5 个核心 Qt6 .so（带完整版本号 + symlink）---
QT6_CORES=(
    "libQt6Core.so.6"
    "libQt6Gui.so.6"
    "libQt6Widgets.so.6"
    "libQt6DBus.so.6"
    "libQt6XcbQpa.so.6"
)

for soname in "${QT6_CORES[@]}"; do
    # 找实际文件（如 libQt6Core.so.6.8.0）
    realfile=$(find "$QT6_SYS_LIB" -maxdepth 1 -name "${soname}.*" -type f 2>/dev/null | head -1)
    if [ -z "$realfile" ]; then
        echo "  ❌ 找不到 $soname"
        exit 1
    fi
    basename=$(basename "$realfile")
    cp -a "$realfile" "$QT6_BUILTIN_DIR/lib/"
    # 建 soname symlink（libQt6Core.so.6 → libQt6Core.so.6.8.0）
    ln -sf "$basename" "$QT6_BUILTIN_DIR/lib/$soname"
    size=$(ls -lh "$realfile" | awk '{print $5}')
    echo "  ✅ $soname → $basename ($size)"
done

# --- 4b. 复制关键 plugins（platforms + styles）---
# platforms：必须要 libqxcb.so（X11）+ libqwayland-generic.so（Wayland）+ libqminimal.so（fallback）
mkdir -p "$QT6_BUILTIN_DIR/plugins/platforms"
for plugin in libqxcb.so libqwayland-generic.so libqwayland-egl.so libqminimal.so libqoffscreen.so; do
    src="$QT6_SYS_PLUGINS/platforms/$plugin"
    if [ -f "$src" ]; then
        cp -a "$src" "$QT6_BUILTIN_DIR/plugins/platforms/"
        echo "  ✅ platforms/$plugin"
    fi
done

# xcbglintegrations（Qt 6.8 下 libqxcb.so 可能依赖）
if [ -d "$QT6_SYS_PLUGINS/xcbglintegrations" ]; then
    mkdir -p "$QT6_BUILTIN_DIR/plugins/xcbglintegrations"
    cp -a "$QT6_SYS_PLUGINS/xcbglintegrations/"*.so "$QT6_BUILTIN_DIR/plugins/xcbglintegrations/" 2>/dev/null || true
    echo "  ✅ xcbglintegrations"
fi

# styles（Fusion 是 Qt6 默认风格，但 deepin/chameleon 在 openKylin 可能也有用）
mkdir -p "$QT6_BUILTIN_DIR/plugins/styles"
cp -a "$QT6_SYS_PLUGINS/styles/"*.so "$QT6_BUILTIN_DIR/plugins/styles/" 2>/dev/null || true
echo "  ✅ styles"

# --- 4c. 复制 qt.conf ---
cp "$RELEASE_DIR/qt.conf" "$OUT_DIR/qt.conf"
echo "  ✅ qt.conf"

QT6_SIZE=$(du -sh "$QT6_BUILTIN_DIR" | cut -f1)
echo ""
echo "  📦 Qt6 内置总大小: $QT6_SIZE"

# ============================================================================
# Step 5: 组装发布目录
# ============================================================================
echo ""
echo "=== Step 5: 组装发布目录 ==="

mkdir -p "$OUT_DIR/bin"
mkdir -p "$OUT_DIR/lib"
mkdir -p "$OUT_DIR/java/classes"
mkdir -p "$OUT_DIR/java/libs"
mkdir -p "$OUT_DIR/jre"
mkdir -p "$OUT_DIR/fonts"

# 二进制
cp "$EDITOR_BUILD/ofd-qt6-editor"   "$OUT_DIR/bin/"
cp "$JNI_BUILD/libofd-jna-core.so"  "$OUT_DIR/lib/"

# Java
cp -r "$BASE/ofd-jna-core/target/classes"/* "$OUT_DIR/java/classes/" 2>/dev/null || true
cp "$RELEASE_DIR/libs"/*.jar "$OUT_DIR/java/libs/" 2>/dev/null || true

# JRE
cp -r "$RELEASE_DIR/jre"/* "$OUT_DIR/jre/"

# 字体
cp -r "$BASE/ofd-qt6-editor/fonts/"* "$OUT_DIR/fonts/" 2>/dev/null || true

# 启动脚本
cp "$RELEASE_DIR/ofd-editor" "$OUT_DIR/bin/"
chmod +x "$OUT_DIR/bin/ofd-editor"

# ============================================================================
# Step 6: deb 打包（无 Qt6 依赖！已内置）
# ============================================================================
echo ""
echo "=== Step 6: deb 打包 ==="

DEB_STAGING="$RELEASE_DIR/deb-staging"
rm -rf "$DEB_STAGING"
mkdir -p "$DEB_STAGING/DEBIAN"
mkdir -p "$DEB_STAGING/usr/bin"
mkdir -p "$DEB_STAGING/usr/share/applications"
mkdir -p "$DEB_STAGING/usr/share/icons/hicolor/scalable/apps"
mkdir -p "$DEB_STAGING/opt/ofd-editor"

# control: 关键 —— 不依赖系统 Qt6（已内置），只依赖基础系统库
cat > "$DEB_STAGING/DEBIAN/control" << 'CTRL'
Package: ofd-editor
Version: PLACEHOLDER_VERSION
Section: office
Priority: optional
Architecture: amd64
Maintainer: Kelvinxi <kelvinxi@outlook.com>
Depends: libgl1, libdbus-1-3, libx11-6, libxcb1, libfontconfig1, libc6 (>= 2.34)
Suggests: fonts-noto-cjk
Description: OFD 阅读器+简易编辑器（openKylin 版，内置 Qt6 运行时）
 基于 Qt6 构建的 OFD 电子文档阅读器和简易编辑器。
 支持打开、浏览、渲染 OFD 文件，支持增值税电子普通发票
 完整渲染（文字、Path 细线、表格、二维码、嵌套公章）。
 本包内置裁剪 JRE（jlink）和 Qt6 运行时，无需预装 Java 或 Qt。
 兼容 openKylin 2.0 SP2 / Ubuntu 24.04+ / Debian 12+ / Deepin/UOS V25。
CTRL
sed -i "s/PLACEHOLDER_VERSION/$VERSION/" "$DEB_STAGING/DEBIAN/control"

# scripts
cp "$BASE/debian/postinst" "$DEB_STAGING/DEBIAN/"
cp "$BASE/debian/prerm"    "$DEB_STAGING/DEBIAN/"
chmod 755 "$DEB_STAGING/DEBIAN/postinst" "$DEB_STAGING/DEBIAN/prerm"

# desktop + 图标
cp "$BASE/debian/ofd-editor.desktop" "$DEB_STAGING/usr/share/applications/"
SRC_ICON="$BASE/ofd-qt6-editor/icons/app.svg"
[ -f "$SRC_ICON" ] && cp "$SRC_ICON" "$DEB_STAGING/usr/share/icons/hicolor/scalable/apps/ofd-editor.svg"

# 应用文件
cp -r "$OUT_DIR"/* "$DEB_STAGING/opt/ofd-editor/"

# symlink
ln -sf "../../opt/ofd-editor/bin/ofd-editor" "$DEB_STAGING/usr/bin/ofd-editor"

# 权限
chmod 755 "$DEB_STAGING/opt/ofd-editor/bin/"*
chmod 755 "$DEB_STAGING/opt/ofd-editor/jre/bin/"* 2>/dev/null || true
chmod 755 "$DEB_STAGING/opt/ofd-editor/qt6/lib/"*.so* 2>/dev/null || true
find "$DEB_STAGING/opt/ofd-editor/bin" -type f -exec chmod +x {} \;

# 打包
DEB_OUT="$RELEASE_DIR/out/ofd-editor_${VERSION}_${ARCH}.deb"
if command -v fakeroot >/dev/null 2>&1; then
    fakeroot dpkg-deb --build --root-owner-group "$DEB_STAGING" "$DEB_OUT"
else
    dpkg-deb --build --root-owner-group "$DEB_STAGING" "$DEB_OUT" 2>&1
fi

rm -rf "$DEB_STAGING"

# ============================================================================
# Step 7: 总结
# ============================================================================
echo ""
echo "========================================"
echo "✅ 构建完成 ($ARCH / $VERSION)"
echo "========================================"
echo "deb: $DEB_OUT ($(du -sh "$DEB_OUT" 2>/dev/null | cut -f1))"
echo "总大小: $(du -sh "$OUT_DIR" | cut -f1)"
echo ""
echo "目录结构:"
du -sh "$OUT_DIR"/*/ 2>/dev/null
echo ""
echo "=== 内置 Qt6 验证 ==="
echo "qt6/lib:"
ls -lh "$OUT_DIR/qt6/lib/" 2>/dev/null
echo ""
echo "qt6/plugins/platforms:"
ls "$OUT_DIR/qt6/plugins/platforms/" 2>/dev/null
echo ""
echo "qt.conf 内容:"
cat "$OUT_DIR/qt.conf" 2>/dev/null
echo ""
echo "=== ldd 验证（应该优先找内置 Qt6）==="
ldd "$OUT_DIR/bin/ofd-qt6-editor" | grep -i qt6