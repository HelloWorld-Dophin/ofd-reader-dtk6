# 更新全套 DeepSeek‑V4‑Flash 分步提示词

# 更新全套 DeepSeek‑V4‑Flash 分步提示词

> 重要变更：**上层 GUI 完全基于 DTK6（Deepin Tool Kit 6），不再使用原生 Qt Widget 原生控件，使用 DMainWindow、DWidget、DFileDialog、DSlider、DPropertyBrowser 等 DTK6 组件；底层不变 ofdrw‑full 2\.0\.2 \+ JNA FFI；运行编译目标 Deepin Linux**
> 调用参数：`temperature=0.0`，`reasoning_effort="high"`
> 硬性基准测试：增值税电子普通发票 OFD，表格 Path 线条、二维码、红色监制图片必须完整渲染。
> 约束：禁止手写 OFD XML 解析、禁止手写 zip 打包；不实现签章验签和国密加密；每轮务必带上**系统提示词**，粘贴上一轮全部代码上下文，完成编译 \+ 样例测试再进入下一轮。
> 
> 

## 🔹全局固定 System 提示词（每轮对话最开头粘贴）

```Plain Text
你是专业Java/C++开发工程师，面向Deepin DTK6信创桌面开发。
项目：OFD阅读器+简易编辑器。
底层固定：ofdrw‑full 2.0.2（OFD全部解析/生成逻辑），JNA5.14实现Java‑C FFI桥接。
GUI层**完全使用DTK6(Deepin Tool Kit 6)**，禁止使用原生Qt普通控件，窗口、对话框、滑块、属性面板全部使用DTK组件 DMainWindow、DWidget、DFileDialog、DSlider、DPropertyBrowser、DMessageBox。
硬性测试基准：必须正常打开国家税务总局增值税电子普通发票OFD，Path表格细线、二维码图片、红色税务监制图片完整渲染，位置对齐官方税务阅读器。
约束：禁止手写OFD XML解析逻辑、禁止手写zip打包。不要实现电子签章验签、国密加密OFD。
所有Java句柄、FFI分配的字符串、数组必须配套提供free释放接口，严防内存泄漏。
输出完整可编译源码，同时输出Deepin apt依赖包清单、编译命令；关键位置增加异常捕获错误处理。
```

---

## 第 1 轮：Maven Java 项目骨架，JNA 基础 FFI，OFD 打开 / 关闭 / 获取页数（底层桥接，无 UI）

> 用户输入 Prompt
> 
> 

```Plain Text
基于ofdrw‑full 2.0.2 + JNA 5.14，生成完整Maven项目 ofd‑jna‑core。
1. 输出完整pom.xml，配置ofdrw‑full、JNA依赖。
2. Java侧通过JNA Native.register导出C风格FFI函数，使用long作为文档、页面句柄，不暴露原始Java对象。
首批导出C函数：
- ofd_open_file：输入文件路径字符串，返回文档句柄；打开OFD失败返回0。
- ofd_close_doc：释放文档句柄，关闭OFD，释放全部资源。
- ofd_get_page_count：输入文档句柄，返回总页面数量。
- ofd_get_last_error：获取错误信息字符串
- ofd_free_string：释放FFI分配字符串内存，避免内存泄漏
3. Java全部代码捕获异常，异常写入错误缓冲区。
4. 输出项目目录结构，Deepin环境Maven编译命令，输出jar包。
本阶段只完成打开、关闭、获取页数，不要做渲染，不要编辑逻辑。
输出全部完整源码：pom.xml、Java源文件。
```

✅验收：编译成功，命令行调用 FFI 接口，打开增值税电子发票 OFD，可以正确读取页面数量。

## 第 2 轮：FFI 导出页面元素解析接口，解析 Text / Image / Path 对象（发票表格 Path 核心）

> 用户输入 Prompt
> 
> 

```Plain Text
继续扩展 ofd‑jna‑core Java项目，复用第1轮完整代码。
解析OFD页面全部内容对象：TextObject文本、ImageObject图片、PathObject路径（电子发票表格全部依靠Path绘制）。
1. JNA定义C映射结构体：
OfdTextItem：文字内容、字号、ARGB颜色、CTM变换矩阵6个浮点数、坐标、字体ID
OfdImageItem：资源ID、宽高、CTM矩阵、图片原始二进制字节
OfdPathItem：路径指令序列M/L/C、描边色、填充色、描边宽度、CTM矩阵
2. 新增FFI导出C函数：
ofd_read_page_elements(long docHandle, int pageIndex)，解析指定页面，输出页面全部文本、图片、Path对象集合。
3. 全部底层逻辑复用ofdrw，遍历页面Content对象，完整提取CTM变换矩阵，完整透传全部参数给上层。
4. 重点：完整解析Path描边宽度、路径指令；读取OFD Res目录内嵌图片二进制数据。
5. 所有FFI分配的数组、字符串，配套ofd_free_xxx释放接口，杜绝内存泄漏。
6. 捕获解析异常，错误通过ofd_get_last_error返回。
输出修改/新增全部Java源码，说明JNA结构体映射关系。
```

✅验收：调用接口读取发票 OFD，可以读出大量 Path 表格线条对象、二维码、红章图片对象。

## 第 3 轮：FFI 导出编辑接口，内存 DOM 修改，保存输出标准 OFD

> 用户输入 Prompt
> 
> 

```Plain Text
继续扩展 ofd‑jna‑core项目，基于ofdrw内存DOM模型实现编辑能力，新增FFI C导出接口。
接口列表：
1. ofd_modify_text(long docHandle, int pageIdx, int textItemIndex, const char* newText, double fontSize, int colorARGB) 修改已有文本
2. ofd_add_text(long docHandle, int pageIdx, double x, double y, const char* text, double fontSize, int colorARGB) 新增文本对象
3. ofd_add_rect_path(long docHandle,int pageIdx,double x,double y,double w,double h,double strokeWidth,int strokeColor,int fillColor) 新增矩形矢量图形
4. ofd_delete_object(long docHandle,int pageIdx,int objIndex) 删除页面指定索引元素
5. ofd_add_page(long docHandle) 新增空白页
6. ofd_save_to_file(long docHandle, const char* outputPath) 将内存DOM另存输出标准OFD文件，全部交给ofdrw完成zip打包，禁止手写zip。

约束：
1. 所有修改操作修改ofdrw内存DOM，禁止直接拼接XML字符串。
2. 保存输出OFD必须符合GB/T33190‑2016；保存后的ofd文件，税务官方阅读器打开不能丢失Path、图片资源。
3. 全部函数增加异常捕获，失败返回0，错误信息写入错误缓冲区。
输出修改完成全部Java源代码。
```

✅验收：修改发票 OFD 文本，另存新 OFD；税务阅读器打开，修改生效、表格和图片完整不丢失。

## 第 4 轮：DTK6 C\+\+ 项目，FFI 桥接封装层 CMake 模板（DTK6 依赖）

> 用户输入 Prompt
> 
> 

```Plain Text
生成基于 **DTK6（Deepin Tool Kit6）** 的C++项目 ofd‑dtk6‑editor，CMake构建，运行目标Deepin Linux。
⚠️ 不要使用原生Qt控件，依赖DTK6库。
1. C++封装类 OfdJnaBridge，封装1‑3轮全部FFI接口，RAII管理OFD文档句柄，自动释放资源，封装JNA动态库加载逻辑。
2. C++结构体与JNA侧C结构体一一对应：OfdTextItem、OfdImageItem、OfdPathItem。
3. FFI调用失败返回错误码或者抛出C++异常。
4. 编写完整CMakeLists.txt：正确查找DTK6组件，链接JNA动态库；输出Deepin需要执行的apt安装依赖包清单。
5. 输出头文件 ofd_jna_bridge.h，源文件 ofd_jna_bridge.cpp。
6. 编写简单DTK6 DMainWindow最小测试程序：打开增值税电子普通发票OFD，读取第0页元素，打印元素数量。

输出全部源码，CMake配置，完整编译运行步骤。
```

✅验收：DTK6 程序编译运行成功；加载 JNA 动态库，成功读取发票 OFD 页面元素数量。

## 第 5 轮：DTK6 自定义渲染控件，继承 DWidget 实现 OFD 渲染，处理 72DPI 与 CTM 矩阵

> 用户输入 Prompt
> 
> 

```Plain Text
继续DTK6 C++项目，创建OfdRenderWidget，继承DTK6的DWidget，重写paintEvent，使用QPainter做绘制。
硬性约束：OFD标准单位72DPI；完整实现CTM变换矩阵转换；缩放画布时极细Path描边不能消失。
1. OfdRenderWidget成员：文档句柄、当前页码、缩放系数。
2. paintEvent逻辑：遍历页面 OfdTextItem、OfdImageItem、OfdPathItem：
①文本对象：QTransform还原OFD CTM矩阵，设置字体颜色字号绘制文字
②图片对象：解析二进制图片数据生成QImage，应用矩阵变换绘制
③Path路径对象：解析M/L/C路径指令生成QPainterPath；设置描边宽度、描边、填充颜色；处理极细描边，缩放后线条依旧可见。
3. 实现坐标转换：窗口屏幕坐标 ↔ OFD文档原始坐标。
4. 实现画布缩放功能。
5. 当前只做渲染，不写编辑交互。
测试基准：增值税电子普通发票OFD渲染效果，和税务官方阅读器肉眼对齐：表格线条完整、左上角二维码、顶部红色监制图片位置正确。
输出头文件、cpp全部源码，修改CMakeLists，给出使用示例。
```

✅验收：DTK 窗口渲染发票 OFD，表格线、二维码、红章全部正常，缩放细线不会消失。

## 第 6 轮：DTK6 完整主窗口、UI 交互、元素选中、编辑属性面板

> 用户输入 Prompt
> 
> 

```Plain Text
扩展DTK6项目，完成主窗口界面，全部UI组件使用DTK6控件，禁止原生Qt控件。
1. 主窗口继承 DMainWindow。
菜单栏使用DTK组件：打开OFD文件(DFileDialog)、另存OFD(DFileDialog)。
页码切换控件、缩放滑块使用 DSlider。
右侧使用 DPropertyBrowser 属性面板。
弹窗提示全部使用 DMessageBox。
2. 扩展 OfdRenderWidget(DWidget)鼠标交互：鼠标点击检测命中页面元素；选中元素绘制虚线选框。
3. DPropertyBrowser属性面板：选中文本元素时，可修改文字内容、字号、颜色。
4. 增加DTK按钮功能：新增文本框、新增矩形图形、删除选中对象、新增空白页面；按钮调用OfdJnaBridge编辑接口，执行完成刷新OfdRenderWidget画布。
5. 捕获FFI异常，使用DMessageBox弹出错误提示。
输出所有新增/修改完整源码。
```

✅验收：DTK 软件可以打开发票 OFD；选中、修改文字；新增图形；删除元素；另存 OFD 文件。

## 第 7 轮：专项 Bug 修复调优 Prompt（渲染错位、Path 丢失、内存泄漏、保存资源丢失时使用）

> 用户输入 Prompt
> 
> 

```Plain Text
针对DTK6+ofdrw‑JNA项目做专项修复，测试样例：增值税电子普通发票OFD。
逐项排查修复：
1. CTM矩阵转换到QTransform计算正确性，解决单元格文字错位偏移。
2. Path路径渲染：缩放后极细描边线条消失问题。
3. 编辑新增图片/矩形后保存OFD：确认图片、Path资源正确写入ofdrw内存DOM；保存后的OFD使用税务阅读器打开，图片矢量图形不会丢失。
4. JNA‑Java‑C层内存检查：所有FFI分配字符串、数组全部调用配套free接口释放，排查内存泄漏。
5. 字体策略：ofdrw读取OFD内嵌字体，DTK渲染层做系统字体回退，发票金额数字排版不能错位乱码。
输出修复后的完整代码，写明每一处修改点。
```

## 第 8 轮：做**发布打包改造**，只处理运行时、jlink、JNA 加载自定义 jre、deb 安装包

> 用户输入 Prompt
> 
> 

```Plain Text
项目已经全部开发完成，ofdrw+JNA+DTK6全部功能本地验证完毕。
现在做发布改造，目标：**目标用户电脑不需要预先安装Java**。
要求：
1. 使用JDK17 jlink，生成裁剪后的自定义JRE，随软件一起分发。
2. 修改JNA加载逻辑：优先加载程序自带目录内的libjvm.so，禁止自动查找系统Java虚拟机。
3. 编写启动shell脚本：先设置环境变量，指向自带jre，再启动DTK6 C++主程序。
4. 编写deb包配置（debian/control、rules），deb声明DTK6运行库依赖；打包把可执行程序、jlink裁剪jre、jar全部打进deb。
5. 输出完整jlink执行命令、目录部署结构、deb打包流程。
6. 提供测试方案：干净Deepin虚拟机，不安装任何JDK/JRE，安装deb，运行程序，使用增值税电子普通发票OFD做完整读写保存验证。
不改动原有业务逻辑（解析、渲染、编辑代码），只新增打包、加载、脚本相关代码。
```
---

# DTK6 Deepin 系统 apt 依赖提示（CMake 需要找到 DTK6）

编译前需要安装，写进每轮输出说明：

```bash
sudo apt install libdtk6-dev dtk6-core-dev dtk6-widgets-dev dtk6-gui-dev openjdk‑17‑jdk maven cmake g++
```

## ⚠️DeepSeek‑V4‑Flash 使用特别注意

1. Flash 对 DTK6 类名容易幻觉写错，检查：`DMainWindow`、`DWidget`、`DFileDialog`、`DPropertyBrowser`，不能写成 Qt 原生`QMainWindow`；

2. JNA 跨语言内存释放是高频出错点，每轮重点核对；

3. 每轮必须把上一轮全部代码粘贴进上下文，不要让模型凭空重写全部工程；

4. 迭代顺序：底层 FFI → DTK 渲染控件 → DTK 完整 UI，**永远拿电子发票 OFD 作为第一优先级测试用例**。

如果你需要，我可以输出第 4 轮的基础 DTK6 CMakeLists\.txt 模板，直接复制丢给 DeepSeek‑V4‑Flash。

> （注：部分内容可能由 AI 生成）
