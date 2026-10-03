# 🎉 OFD 阅读器（编辑器）— Deepin DTK6 原生应用

> 一款基于 **Deepin Tool Kit 6** 构建的 OFD 电子文档阅读器和简易编辑器，零预装依赖，下载即用。
>
> **🚀开源地址：<https://github.com/HelloWorld-Dophin/ofd-reader-dtk6>**

## **📷️运行截图：**

![dtk6-screenshot.png](assets/screenshots/dtk6-screenshot.png)

## 🚀 安装命令：Deepin/UOS 安装包（88MB，含内置 JRE + 字体 + 所有依赖）

```bash
sudo dpkg -i ofd-editor_1.0.0_amd64.deb
```

***

## 📖 关于 OFD

OFD（Open Fixed-layout Document）是我国自主可控的电子文档版式国家标准（GB/T 33190-2016），广泛应用于：

- 📄 **增值税电子普通发票 / 专用发票**（全电发票）
- 🏛️ 党政机关电子公文
- 📊 银行对账单、保险保单
- 🎓 电子证照、学历证书

## ✨ 功能特性

### 📚 文档渲染

| 特性                               | 状态     |
| -------------------------------- | ------ |
| 文字对象（TextObject）渲染               | ✅ 完整支持 |
| 路径对象（PathObject）细线、填充、渐变色        | ✅ 完整支持 |
| 图像对象（ImageObject）含 JBIG2 压缩      | ✅ 完整支持 |
| 表格（使用 Path 细线模拟）                 | ✅ 正确对齐 |
| 嵌套印章 / 签名注解（SignatureAnnotation） | ✅ 完整支持 |
| 多页浏览、翻页、页面切换                     | ✅      |
| 缩放（25% - 400%，含"适合宽度/高度/页面"）     | ✅      |
| 页面旋转（90° 步进）                     | ✅      |
| 高 DPI 自适应                        | ✅      |

### ✏️ ~~编辑能力~~（正在开发）

| ~~特性~~                       | ~~状态~~ |
| ---------------------------- | ------ |
| ~~文字对象属性编辑（字体 / 字号 / 颜色）~~   | ~~✅~~  |
| ~~图形元素属性编辑（填充色 / 描边色 / 线宽）~~ | ~~✅~~  |
| ~~选中元素（鼠标框选 + 点击命中）~~        | ~~✅~~  |
| ~~页面元素列表展示（便于精确定位）~~         | ~~✅~~  |
| ~~另存为新 OFD 文件~~              | ~~✅~~  |

### 🎨 界面体验

- **DTK6 原生**：DMainWindow、DTitlebar、DStatusBar、DPropertyBrowser、DMessageBox —— 完全贴合 Deepin 桌面风格
- **中文字体 Tab**：楷体 / 黑体 / 宋体 / 仿宋 四大常用字体独立切换下拉框
- **内置 8 款开源字体**：发布包自带（ZhenKai\_GBK、SimXiHei Plus、SimZhiSong、Zhuque Fangsong、Liberation Mono 系列），无需系统预装
- **深色 / 浅色主题自适应**：跟随系统切换

***

## 🏗️ 技术架构

```
┌──────────────────────────────────────────────────────────────┐
│                   DTK6 C++ 主程序 (ofd-dtk6-editor)           │
│                                                              │
│  ┌──────────────┐   ┌──────────────┐   ┌──────────────────┐  │
│  │  MainWindow  │   │  OfdRender   │   │  PropertyPanel   │  │
│  │  (UI 组装)   │   │  Widget      │   │  (属性编辑)      │  │
│  └──────┬───────┘   │  (QPainter)  │   └────────┬─────────┘  │
│         │           └──────┬───────┘            │            │
│         │                  │                     │            │
│         ▼                  ▼                     ▼            │
│  ┌─────────────────────────────────────────────────────┐     │
│  │          OfdJnaBridge (JNI C ABI 桥接)              │     │
│  │   dlopen → libofd-jna-core.so → JNA → Java 层       │     │
│  └──────────────────────────┬──────────────────────────┘     │
└─────────────────────────────┼────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                Java 层 (ofd-jna-core)                        │
│                                                             │
│   JNA 5.14 接口绑定          ofdrw-full 2.0.2               │
│   ├── OFDFfi.java            ├── OfdReader (文档打开/解析)   │
│   ├── OfdPageElements       ├── OfdPathParser (Path 解析)   │
│   ├── OfdImageLoader        └── OfdFontResolver             │
│   └── OfdException          (全模块: reader/core/pkg/layout  │
│                               /gv/sign/font/graphics2d)     │
└─────────────────────────────────────────────────────────────┘
```

### 技术栈一览

| 层级     | 技术                     | 说明                                         |
| ------ | ---------------------- | ------------------------------------------ |
| GUI    | **DTK6 + Qt 6.8**      | Deepin Tool Kit 6，原生 C++ 组件                |
| FFI    | **JNA 5.14**           | Java ↔ C 跨语言调用                             |
| 原生桥接   | **JNI C**              | ofd\_jna\_wrapper.c + ofd\_jna\_bridge.cpp |
| OFD 解析 | **ofdrw-full 2.0.2**   | org.ofdrw 官方开源库（禁止使用 com.github.ofdrw）     |
| JRE    | **OpenJDK 17 + jlink** | 裁剪后仅\~50MB，随软件分发                           |
| 打包     | **dpkg-deb**           | 纯二进制打包，含内置 JRE + 所有依赖                      |

### 🔑 几个关键设计决策

**1. JNA FFI 而非 JNI**

直接用 JNI 手写原生方法工作量大且维护困难。JNA 允许在 C++ 侧 `dlopen` 加载 `.so` 后直接调用，Java 侧只用一个 interface 声明方法签名，自动映射。

**2. 内置裁剪 JRE（零预装依赖）**

用 JDK17 的 `jlink` 工具从完整 JDK 裁剪出仅包含运行所需模块的 JRE：

```bash
jlink \
    --add-modules java.base,java.desktop,java.xml,java.logging,java.management,\
                 java.security.sasl,java.net.http,jdk.unsupported,jdk.zipfs,\
                 jdk.crypto.ec,java.prefs,java.naming,java.scripting \
    --compress 2 --strip-debug --no-header-files --no-man-pages \
    --output ./jre
```

产物 \~50MB（完整 JDK17 通常 300MB+）。

**3. LD\_LIBRARY\_PATH 覆盖 RUNPATH**

`libofd-jna-core.so` 硬编码了 RUNPATH 指向系统 JDK，但 ELF 动态链接器优先级是 **LD\_LIBRARY\_PATH > DT\_RUNPATH**。启动脚本只需：

```bash
export LD_LIBRARY_PATH="$APP_HOME/jre/lib/server:$APP_HOME/lib:$LD_LIBRARY_PATH"
```

就能强制 JVM 加载内置的 `libjvm.so`，**用户电脑完全不需要预装 Java**。

**4. symlink 安全的启动脚本**

`.desktop` 的 `Exec=/usr/bin/ofd-editor` 实际是 symlink → `/opt/ofd-editor/bin/ofd-editor`。脚本必须用 `readlink -f "$0"` 解析到真实物理路径，否则 `$0` 会拿到 `/usr/bin/ofd-editor`，APP\_HOME 算成 `/usr` —— 所有路径全部错。

**5. 字体路径多候选**

可执行文件在 `bin/` 下，字体在 `bin/../fonts/`。开发版和发布版目录结构不同，字体加载函数同时扫描 3 个候选路径：

- `../fonts`（发布版）
- `./fonts`（开发版）
- `$OFD_EDITOR_FONTS`（环境变量覆盖）

***

## 📦 安装 & 运行

```bash
sudo dpkg -i ofd-editor_1.0.0_amd64.deb
```

deb 包体积 \~88MB，包含：

- DTK6 C++ 主程序
- JNI C 原生 `.so`
- Java 层所有依赖（38 个 jars：ofdrw 全模块 + JNA + BouncyCastle + zip4j + ...）
- **内置裁剪 JRE**（\~50MB）
- 8 款内置字体
- SVG 图标 + `.desktop` 入口

**不需要预装 OpenJDK！** 系统只会自动拉取 Qt6/DTK6 运行时依赖。

### 目录结构（安装后）

```
/opt/ofd-editor/              ← 程序主体
├── bin/
│   ├── ofd-dtk6-editor      ← C++ ELF 主程序
│   └── ofd-editor           ← Bash shebang 启动脚本（应用程序格式，无后缀）
├── lib/
│   └── libofd-jna-core.so   ← JNI C ABI 桥接
├── java/
│   ├── classes/             ← Java 编译产物
│   └── libs/                ← 38 个依赖 jars
├── jre/                     ← jlink 裁剪版 JRE（50MB，无 javac）
│   ├── bin/java
│   └── lib/server/libjvm.so ← JVM 本体
├── fonts/                   ← 8 款内置字体
└── share/icons/

/usr/bin/ofd-editor → /opt/ofd-editor/bin/ofd-editor   ← symlink 入口
/usr/share/applications/ofd-editor.desktop             ← 桌面入口
/usr/share/icons/hicolor/scalable/apps/ofd-editor.svg   ← 应用图标
```

### 启动方式

```bash
# 命令行
/usr/bin/ofd-editor /path/to/document.ofd

# 或双击桌面图标 / 任务栏点击 / 开始菜单搜索 "OFD"
```

***

## 🖼️ 渲染质量对比

### 测试用例：国家税务总局增值税电子普通发票

| 项目           | 官方阅读器 | 本项目 | 说明                     |
| ------------ | ----- | --- | ---------------------- |
| Path 表格细线    | ✅     | ✅   | 位置、粗细、颜色一致             |
| 二维码图片（JBIG2） | ✅     | ✅   | 完整显示                   |
| 红色税务监制章      | ✅     | ✅   | 位置、颜色对齐                |
| 中文字体渲染       | ✅     | ✅   | 字体别名映射 + 内置字体 fallback |
| 渐变色填充        | ✅     | ✅   | Path 的 Gradient 解析完整   |

***

## 🚧 当前限制

- ⚠️ 不支持电子签章验签（国密算法 SM2/SM4 不在范围）
- ⚠️ 不支持加密 OFD（EncryptedPackage）
- ~~⚠️ 编辑功能为"简易"级别（主要解决渲染 + 属性编辑 + 另存为），非完整 OFD 编辑器~~
- ⚠️ Windows / macOS 暂未适配（当前仅限 Deepin/UOS Linux x64）

***

## 🗺️ 未来计划

- [ ] Windows 版本适配（MSYS2 + Qt6 + 内置 JRE）
- [ ] 编辑能力：拖拽移动元素、复制粘贴、撤销重做
- [ ] 打印预览 + 直接打印
- [ ] 文档缩略图侧栏
- [ ] 最近打开列表（QSettings）
- [ ] 插件化架构（支持自定义渲染器 / 编辑器）

***

## 🤝 贡献

欢迎反馈问题！

- 博客：<https://www.cnblogs.com/Kelvinxi/>
- 邮件：`kelvinxi@outlook.com`
- 开源地址：<https://github.com/HelloWorld-Dophin/ofd-reader-dtk6>

## 📄 开源许可

本项目采用 **Apache License 2.0** 开源，可自由用于商业用途。

依赖说明：

- ofdrw-full 2.0.2（Apache 2.0）
- JNA 5.14（Apache 2.0 / LGPL 2.1 双许可）
- DTK6（LGPL 3.0）
- 内置字体：ZhenKai\_GBK / SimXiHei Plus / SimZhiSong / Zhuque Fangsong / Liberation Mono（均为开源免费字体）

***

## 💖 致谢

- **ofdrw** 开源作者 —— 优秀的 OFD Java 库，让我们不用手写 XML/ZIP 解析
- **Deepin DTK6** 团队 —— 原生 C++ 桌面框架，让 Deepin 应用开发体验接近 Qt 原生
- **JNA 社区** —— 跨语言 FFI 的标准方案

