#!/bin/bash
# ============================================================================
# build-release.sh — 一键构建发布包（含内置 JRE，无需用户预装 Java）
#
# 产物：release/out/ofd-editor/
#        ├── bin/                    DTK6 C++ 主程序
#        ├── lib/                    原生 so（libofd-jna-core.so）
#        ├── java/                   Java 产物
#        │   ├── classes/            ofd-jna-core 编译输出
#        │   └── libs/               所有依赖 jars（ofdrw+jna+bouncycastle+...）
#        ├── jre/                    jlink 裁剪的 JRE (~50MB)
#        ├── fonts/                  内置字体
#        └── share/
#            ├── ofd-editor       启动脚本
#            └── icons/             应用图标
# ============================================================================

set -e

# --- 路径 ---
BASE="$(cd "$(dirname "$0")/.." && pwd)"
RELEASE_DIR="$BASE/release"
OUT_DIR="$RELEASE_DIR/out/ofd-editor"

# --- JDK 路径（用系统 JDK 做 jlink + javac） ---
JAVA17_HOME="${JAVA17_HOME:-/usr/lib/jvm/java-17-openjdk-amd64}"
if [ ! -d "$JAVA17_HOME" ]; then
    echo "错误: 找不到 JDK 17 在 $JAVA17_HOME" >&2
    echo "请 export JAVA17_HOME=/path/to/jdk17" >&2
    exit 1
fi

JLINK="$JAVA17_HOME/bin/jlink"

# ============================================================================
# Step 0: 清理旧产物
# ============================================================================
echo "=== Step 0: 清理 ==="
rm -rf "$OUT_DIR"
rm -rf "$RELEASE_DIR/jre"
rm -rf "$RELEASE_DIR/libs"

# ============================================================================
# Step 1: 编译 Java + C++
# ============================================================================
echo ""
echo "=== Step 1: 编译 ==="

# Java (离线)
cd "$BASE/ofd-jna-core"
mvn clean compile -o 2>&1 | tail -3

# C++ native
cd "$BASE/ofd-jna-core-native/build"
cmake --build . -j4 2>&1 | tail -3

# DTK6 主程序
cd "$BASE/ofd-dtk6-editor/build"
cmake --build . -j4 2>&1 | tail -3

# ============================================================================
# Step 2: jlink 裁剪内置 JRE
# ============================================================================
echo ""
echo "=== Step 2: jlink 裁剪 JRE ==="
$JLINK \
    --module-path "$JAVA17_HOME/jmods" \
    --add-modules java.base,java.desktop,java.xml,java.logging,java.management,java.security.sasl,java.net.http,jdk.unsupported,jdk.zipfs,jdk.crypto.ec,java.prefs,java.naming,java.scripting \
    --output "$RELEASE_DIR/jre" \
    --compress 2 \
    --no-header-files \
    --no-man-pages \
    --strip-debug

echo "  JRE 大小: $(du -sh "$RELEASE_DIR/jre" | cut -f1)"

# ============================================================================
# Step 3: 收集依赖 jars（离线模式：从 ~/.m2 拿）
# ============================================================================
echo ""
echo "=== Step 3: 收集依赖 jars ==="
mkdir -p "$RELEASE_DIR/libs"

# 从 ofd-jna-core pom 解析依赖，或者直接扫 ~/.m2
# 关键：不能把 maven-plugin / plexus / junit 等构建时依赖带进去
cd "$RELEASE_DIR/libs"

# 必需：直接依赖
MUST_HAVE=(
    "net/java/dev/jna/jna/5.14.0/jna-5.14.0"
    "net/lingala/zip4j/zip4j/2.11.3/zip4j-2.11.3"
)

# ofdrw 全模块（相对 m2 repo 格式，和其他条目一致）
for mod in reader core pkg layout gv crypto sign font graphics2d converter tool full; do
    MUST_HAVE+=("org/ofdrw/ofdrw-${mod}/2.0.2/ofdrw-${mod}-2.0.2")
done

# 传递依赖（手动枚举，因为 dependency:copy-dependencies 离线不可用）
TRANSITIVE=(
    "org/bouncycastle/bcprov-jdk15on/1.68/bcprov-jdk15on-1.68"
    "org/bouncycastle/bcpkix-jdk15on/1.68/bcpkix-jdk15on-1.68"
    "commons-io/commons-io/2.11.0/commons-io-2.11.0"
    "org/dom4j/dom4j/2.1.3/dom4j-2.1.3"
    "jaxen/jaxen/1.2.0/jaxen-1.2.0"
    "org/slf4j/slf4j-api/1.7.36/slf4j-api-1.7.36"
    "org/apache/commons/commons-compress/1.18/commons-compress-1.18"
    "org/tukaani/xz/1.8/xz-1.8"
    "org/apache/pdfbox/jbig2-imageio/3.0.3/jbig2-imageio-3.0.3"
    "org/apache/pdfbox/pdfbox/2.0.27/pdfbox-2.0.27"
    "org/apache/pdfbox/fontbox/2.0.27/fontbox-2.0.27"
    "com/twelvemonkeys/common/common-lang/3.5/common-lang-3.5"
    "com/twelvemonkeys/common/common-io/3.5/common-io-3.5"
    "com/twelvemonkeys/common/common-image/3.5/common-image-3.5"
    "com/twelvemonkeys/imageio/imageio-core/3.5/imageio-core-3.5"
    "com/twelvemonkeys/imageio/imageio-metadata/3.5/imageio-metadata-3.5"
    "com/twelvemonkeys/imageio/imageio-tiff/3.5/imageio-tiff-3.5"
    "com/itextpdf/layout/7.1.13/layout-7.1.13"
    "com/itextpdf/font-asian/7.1.13/font-asian-7.1.13"
    "com/itextpdf/kernel/7.1.13/kernel-7.1.13"
    "com/itextpdf/io/7.1.13/io-7.1.13"
    "xalan/xalan/2.7.2/xalan-2.7.2"
    "xalan/serializer/2.7.2/serializer-2.7.2"
    "xml-apis/xml-apis/1.4.01/xml-apis-1.4.01"
)

COUNT=0
for f in "${MUST_HAVE[@]}" "${TRANSITIVE[@]}"; do
    src="$HOME/.m2/repository/${f}.jar"
    if [ -f "$src" ]; then
        cp "$src" .
        COUNT=$((COUNT + 1))
    else
        echo "  ⚠️  缺少: $f"
    fi
done
echo "  收集 jars: $COUNT"

# ============================================================================
# Step 4: 组装目录结构
# ============================================================================
echo ""
echo "=== Step 4: 组装发布目录 ==="

mkdir -p "$OUT_DIR/bin"
mkdir -p "$OUT_DIR/lib"
mkdir -p "$OUT_DIR/java/classes"
mkdir -p "$OUT_DIR/java/libs"
mkdir -p "$OUT_DIR/jre"
mkdir -p "$OUT_DIR/fonts"
mkdir -p "$OUT_DIR/share/icons"

# C++ 主程序 + native so
cp "$BASE/ofd-dtk6-editor/build/ofd-dtk6-editor" "$OUT_DIR/bin/"
cp "$BASE/ofd-jna-core-native/build/libofd-jna-core.so" "$OUT_DIR/lib/"

# Java 编译产物
cp -r "$BASE/ofd-jna-core/target/classes"/* "$OUT_DIR/java/classes/" 2>/dev/null || true

# 依赖 jars
cp "$RELEASE_DIR/libs"/*.jar "$OUT_DIR/java/libs/" 2>/dev/null || true

# JRE
cp -r "$RELEASE_DIR/jre"/* "$OUT_DIR/jre/"

# 字体
cp -r "$BASE/ofd-dtk6-editor/fonts/"* "$OUT_DIR/fonts/" 2>/dev/null || true

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

# control + scripts
cp "$BASE/debian/control" "$DEB_STAGING/DEBIAN/"
cp "$BASE/debian/postinst" "$DEB_STAGING/DEBIAN/"
cp "$BASE/debian/prerm"    "$DEB_STAGING/DEBIAN/"
chmod 755 "$DEB_STAGING/DEBIAN/postinst" "$DEB_STAGING/DEBIAN/prerm"

# desktop + 图标
cp "$BASE/debian/ofd-editor.desktop" "$DEB_STAGING/usr/share/applications/"

# 应用图标（SVG → scalable/apps/，Deepin 完全支持 SVG）
# desktop 文件 Icon=ofd-editor → 找 hicolor/scalable/apps/ofd-editor.svg
SRC_ICON="$BASE/ofd-dtk6-editor/icons/app.svg"
if [ -f "$SRC_ICON" ]; then
    cp "$SRC_ICON" "$DEB_STAGING/usr/share/icons/hicolor/scalable/apps/ofd-editor.svg"
    echo "  图标: scalable/apps/ofd-editor.svg"
fi
# 同时拷贝一份到应用目录内，方便 Qt 运行时 fallback
cp "$SRC_ICON" "$OUT_DIR/share/icons/ofd-editor.svg" 2>/dev/null || true

# 应用文件
cp -r "$OUT_DIR"/* "$DEB_STAGING/opt/ofd-editor/"

# /usr/bin 启动入口
ln -sf "../../opt/ofd-editor/bin/ofd-editor" "$DEB_STAGING/usr/bin/ofd-editor"

# 权限
chmod 755 "$DEB_STAGING/opt/ofd-editor/bin/"*
chmod 755 "$DEB_STAGING/opt/ofd-editor/jre/bin/"* 2>/dev/null || true

# 打包
DEB_OUT="$RELEASE_DIR/out/ofd-editor_1.0.0_amd64.deb"
if command -v fakeroot >/dev/null 2>&1; then
    fakeroot dpkg-deb --build --root-owner-group "$DEB_STAGING" "$DEB_OUT"
else
    dpkg-deb --build --root-owner-group "$DEB_STAGING" "$DEB_OUT" 2>&1
fi

rm -rf "$DEB_STAGING"

echo ""
if [ -f "$DEB_OUT" ]; then
    echo "✅ deb 包: $DEB_OUT ($(du -sh "$DEB_OUT" | cut -f1))"
else
    echo "⚠️  deb 打包失败（需要 fakeroot），可以手动："
    echo "   sudo apt install fakeroot dpkg-dev"
fi

echo ""
echo "=== 完成 ==="
echo "发布目录: $OUT_DIR"
echo "总大小: $(du -sh "$OUT_DIR" | cut -f1)"
echo ""
echo "目录结构:"
du -sh "$OUT_DIR"/*/ 2>/dev/null
