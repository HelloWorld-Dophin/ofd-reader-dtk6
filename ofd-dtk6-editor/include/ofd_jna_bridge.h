/*
 * ofd_jna_bridge.h — OFD JNA FFI C++ 封装
 *
 * 封装 ofd-jna-core JNA 动态库的全部 C ABI 接口，提供：
 *   - C++ RAII 类 OfdJnaBridge：自动加载/卸载 .so，RAII 管理文档句柄
 *   - 与 JNA 侧 OfdStructs 一一对应的 POD 结构体
 *   - 异常类型 OfdException：封装 FFI 错误码 + 错误信息
 *
 * 依赖：libofd-jna-core.so（ofd-jna-core mvn package 产物）
 */
#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <memory>
#include <stdexcept>

namespace ofd {

/* ==================== POD 结构体（与 JNA OfdStructs 严格对齐） ==================== */

/** CTM 变换矩阵：6 个 double，OFD 规范 (a b c d e f)。 */
constexpr int CTM_SIZE = 6;

/**
 * 文本对象 — 64 位布局与 JNA OfdTextItem 完全对齐。
 *
 * 偏移验证（对照 OfdStructs.java）：
 *   content     → 0   (Pointer, 8B)
 *   fontSize    → 8   (double, 8B)
 *   argb        → 16  (int, 4B + 4B pad)
 *   ctm[6]      → 24  (double[6], 48B)
 *   x           → 72  (double, 8B)
 *   y           → 80  (double, 8B)
 *   fontId      → 88  (int32, 4B)
 *   (padding)   → 92  (4B，int32→Pointer 8 对齐)
 *   fontNamePtr → 96  (Pointer, 8B)
 *   sizeof      = 104
 */
#pragma pack(push, 8)
struct OfdTextItem {
    void*       content;        // UTF-8 字符串（FFI 分配，随容器释放）
    double      fontSize;       // 字号（毫米）
    int32_t     argb;           // ARGB 颜色 0xAARRGGBB
    double      ctm[CTM_SIZE];  // CTM 变换矩阵
    double      x;              // 坐标 X（毫米，CTM 本地）
    double      y;              // 坐标 Y（毫米，CTM 本地）
    int32_t     fontId;         // 字体资源 ID（int32）
    double      boundaryWidth;  // TextObject boundary 宽度（毫米）
    double      boundaryHeight; // TextObject boundary 高度（毫米）
    double      contentAdvance; // DeltaX 累计字符间距（毫米），C++ 用它缩放过宽字体
    void*       fontNamePtr;    // 字体名 UTF-8 字符串（JNI 分配，随容器释放），NULL 表示无
    void*       gradient;       // OfdGradient* 或 null（填充色为渐变时）
};

/**
 * 图片对象 — 64 位布局与 JNA OfdImageItem 完全对齐。
 *
 * 偏移验证：
 *   resourceId  → 0   (long, 8B)
 *   width       → 8   (double, 8B)
 *   height      → 16  (double, 8B)
 *   ctm[6]      → 24  (double[6], 48B)
 *   data        → 72  (Pointer, 8B)
 *   dataSize    → 80  (int, 4B + 4B pad)
 *   sizeof      = 88
 */
struct OfdImageItem {
    int64_t     resourceId;     // 图片资源 ID
    double      width;          // 显示宽度（毫米）
    double      height;         // 显示高度（毫米）
    double      ctm[CTM_SIZE];  // CTM 变换矩阵
    void*       data;           // 图片原始二进制（FFI 分配）
    int32_t     dataSize;       // 字节长度
};

/**
 * 路径对象 — 64 位布局与 JNA OfdPathItem 完全对齐（pack=8）。
 *
 * 实测 Java JNA Structure.size() = 104 字节：
 *   data        → 0    (Pointer 8B)
 *   strokeArgb  → 8    (int32 4B)
 *   fillArgb    → 12   (int32 4B)
 *   strokeWidth → 16   (double 8B)
 *   ctm[6]      → 24..71 (double[6] 48B)
 *   strokeFlag  → 72   (int32 4B)
 *   fillFlag    → 76   (int32 4B)
 *   miterLimit  → 80   (double 8B)
 *   capType     → 88   (int32 4B)
 *   joinType    → 92   (int32 4B)
 *   alpha       → 96   (int32 4B)
 *   [tail pad 4B to 104]
 */
struct OfdPathItem {
    void*       data;           // → 0
    int32_t     strokeArgb;     // → 8
    int32_t     fillArgb;       // → 12
    double      strokeWidth;    // → 16
    double      ctm[CTM_SIZE];  // → 24..71
    int32_t     strokeFlag;     // → 72: 0=未设置 1=true 2=false
    int32_t     fillFlag;       // → 76: 0=未设置 1=true 2=false
    double      miterLimit;     // → 80: OFD 默认 10
    int32_t     capType;        // → 88: 0=Butt 1=Round 2=Square
    int32_t     joinType;       // → 92: 0=Miter 1=Round 2=Bevel
    int32_t     alpha;          // → 96: 0-255
    void*       gradient;       // → 100: OfdGradient* 或 null
    // sizeof = 112 (pack=8 下自动对齐)
};

/**
 * 渐变描述（变长结构）。
 *
 * 内存布局：
 *   type       → 0   (int32: 0=none 1=Axial 2=Radial)
 *   startX     → 8   (double)   Axial=StartPoint.x, Radial=CenterPoint.x
 *   startY     → 16  (double)   Axial=StartPoint.y, Radial=CenterPoint.y
 *   endX       → 24  (double)   Axial=EndPoint.x
 *   endY       → 32  (double)   Axial=EndPoint.y
 *   segCount   → 40  (int32: Segment 数量)
 *   [pad 4B]
 *   stops[]    → 48  (OfdGradientStop[segCount])
 *
 * 每个 OfdGradientStop:
 *   position → 0 (double: 0.0-1.0)
 *   argb     → 8 (int32)
 *   sizeof   → 16
 */
struct OfdGradient {
    int32_t     type;           // 0=none 1=Axial 2=Radial
    double      startX;
    double      startY;
    double      endX;
    double      endY;
    int32_t     segCount;       // Segment 数量
    // 后面紧跟 OfdGradientStop[segCount]
};

/**
 * 页面元素容器 — 64 位布局与 JNA OfdPageElements 完全对齐。
 *
 * 重要：texts / images / paths 是 **指针数组**（每个元素是对应 item 的裸指针），
 *      不是连续的 struct 数组！遍历方式：
 *      @code
 *      auto** textPtrs = (void**)elems->texts;
 *      for (int i = 0; i < elems->textCount; ++i) {
 *          auto* item = (OfdTextItem*)textPtrs[i];
 *          // 使用 item->content, item->fontSize ...
 *      }
 *      @endcode
 *
 * 偏移验证：
 *   textCount  → 0   (int, 4B + 4B pad)
 *   texts      → 8   (Pointer, 8B)
 *   imageCount → 16  (int, 4B + 4B pad)
 *   images     → 24  (Pointer, 8B)
 *   pathCount  → 32  (int, 4B + 4B pad)
 *   paths      → 40  (Pointer, 8B)
 *   sizeof     = 48
 */
struct OfdPageElements {
    int32_t     textCount;      // 文本元素数量
    void*       texts;          // OfdTextItem* 指针数组（不是连续 struct）
    int32_t     imageCount;     // 图片元素数量
    void*       images;         // OfdImageItem* 指针数组
    int32_t     pathCount;      // 路径元素数量
    void*       paths;          // OfdPathItem* 指针数组
};
#pragma pack(pop)

/* ==================== C++ 异常 ==================== */

/**
 * OFD 操作异常。
 * 所有 FFI 调用失败时抛出此异常，携带错误码和 FFI 侧最后一条错误信息。
 */
class OfdException : public std::runtime_error {
public:
    OfdException(int code, const std::string& msg)
        : std::runtime_error(msg), code_(code) {}

    int code() const noexcept { return code_; }

private:
    int code_;
};

/* ==================== RAII 句柄包装 ==================== */

/**
 * 页面元素容器 RAII 包装。
 * 构造时持有 void* 指针，析构时自动调用 ofd_free_elements 释放。
 */
class OfdPageElementsGuard {
public:
    /** 空容器，不持有资源。 */
    OfdPageElementsGuard() noexcept = default;

    /** 从裸指针接管所有权。 */
    explicit OfdPageElementsGuard(void* raw) noexcept : raw_(raw) {}

    ~OfdPageElementsGuard();  // 自动调用 free

    // 禁止拷贝，允许移动
    OfdPageElementsGuard(const OfdPageElementsGuard&) = delete;
    OfdPageElementsGuard& operator=(const OfdPageElementsGuard&) = delete;
    OfdPageElementsGuard(OfdPageElementsGuard&& other) noexcept;
    OfdPageElementsGuard& operator=(OfdPageElementsGuard&& other) noexcept;

    /** 释放所有权，返回裸指针。 */
    void* release() noexcept { auto* p = raw_; raw_ = nullptr; return p; }

    /** 裸指针访问（只读）。 */
    const OfdPageElements* get() const noexcept { return reinterpret_cast<const OfdPageElements*>(raw_); }
    OfdPageElements* get() noexcept { return reinterpret_cast<OfdPageElements*>(raw_); }

    bool valid() const noexcept { return raw_ != nullptr; }

private:
    void* raw_ = nullptr;
};

/* ==================== 页面尺寸 ==================== */

/** OFD 页面物理盒子，单位 mm。 */
struct OfdPageSize {
    double x = 0.0;   // 左上角 X
    double y = 0.0;   // 左上角 Y
    double width  = 210.0;
    double height = 297.0;
};

/* ==================== 主桥接类 ==================== */

/**
 * OFD JNA FFI 桥接类。
 *
 * 功能：
 *   - 动态加载 libofd-jna-core.so（构造时加载，析构时卸载）
 *   - RAII 管理文档句柄：打开的文档在析构时自动 close
 *   - 封装全部 FFI 接口，失败抛 OfdException
 *
 * 使用示例：
 *   OfdJnaBridge bridge;                     // 加载 .so
 *   auto doc = bridge.open("/path/doc.ofd"); // 打开文档（RAII 句柄）
 *   auto page = doc.readPage(0);             // 读取第 0 页
 *   // page.texts / page.images / page.paths
 *   doc.modifyText(0, 0, "新文本", 0, 0xFFFF0000);
 *   doc.saveAs("/tmp/out.ofd");
 *   // doc / bridge 析构时自动释放
 */
class OfdJnaBridge {
public:
    /**
     * 加载 JNA 动态库。
     * @param libPath 动态库路径，默认 "ofd-jna-core"（系统搜索路径）。
     * @throws OfdException 加载失败
     */
    explicit OfdJnaBridge(const std::string& libPath = "ofd-jna-core");
    ~OfdJnaBridge();

    // 禁止拷贝，允许移动
    OfdJnaBridge(const OfdJnaBridge&) = delete;
    OfdJnaBridge& operator=(const OfdJnaBridge&) = delete;
    OfdJnaBridge(OfdJnaBridge&&) noexcept;
    OfdJnaBridge& operator=(OfdJnaBridge&&) noexcept;

    // ==================== 文档 RAII 句柄 ====================

    class Doc {
    public:
        Doc() noexcept = default;
        ~Doc();

        Doc(Doc&& other) noexcept;
        Doc& operator=(Doc&& other) noexcept;
        Doc(const Doc&) = delete;
        Doc& operator=(const Doc&) = delete;

        bool valid() const noexcept { return handle_ != 0; }
        int64_t handle() const noexcept { return handle_; }

        // ==================== 读取 ====================

        /** 总页数。 */
        int pageCount() const;

        /** 指定页面的物理尺寸 (mm)。 */
        OfdPageSize pageSize(int pageIndex) const;

        /**
         * 读取指定页面元素。
         * @param pageIndex 页面索引（0 起始）
         * @return RAII 包装的页面元素容器
         */
        OfdPageElementsGuard readPage(int pageIndex) const;

        // ==================== 编辑 ====================

        /** 修改已有文本对象。 */
        void modifyText(int pageIdx, int textItemIndex,
                        const std::string& newText,
                        double fontSize, int32_t colorARGB);

        /** 新增文本对象。 */
        void addText(int pageIdx, double x, double y,
                     const std::string& text,
                     double fontSize, int32_t colorARGB);

        /** 新增矩形矢量图形。 */
        void addRectPath(int pageIdx, double x, double y,
                         double w, double h, double strokeWidth,
                         int32_t strokeColor, int32_t fillColor);

        /** 删除页面指定索引元素。 */
        void deleteObject(int pageIdx, int objIndex);

        /** 新增空白页。 */
        void addPage();

        /** 另存为标准 OFD 文件。 */
        void saveAs(const std::string& outputPath);

    private:
        friend class OfdJnaBridge;
        Doc(OfdJnaBridge* bridge, int64_t handle) noexcept
            : bridge_(bridge), handle_(handle) {}

        OfdJnaBridge* bridge_ = nullptr;
        int64_t       handle_ = 0;
    };

    // ==================== 公开 API ====================

    /**
     * 打开 OFD 文档。
     * @param path 文件路径（UTF-8）
     * @return RAII 文档句柄
     * @throws OfdException 打开失败
     */
    Doc open(const std::string& path);

private:
    /* ============ FFI 函数指针类型 ============ */
    using FnOpenFile       = int64_t (*)(const char*);
    using FnCloseDoc       = int     (*)(int64_t);
    using FnGetPageCount   = int     (*)(int64_t);
    using FnGetPageSize    = int     (*)(int64_t, int, double*, double*, double*, double*);
    using FnGetLastError   = void*   (*)();
    using FnFreeString     = void    (*)(void*);
    using FnReadPageElems  = void*   (*)(int64_t, int);
    using FnFreeElements   = void    (*)(void*);
    using FnModifyText     = int     (*)(int64_t, int, int, const char*, double, int);
    using FnAddText        = int     (*)(int64_t, int, double, double, const char*, double, int);
    using FnAddRectPath    = int     (*)(int64_t, int, double, double, double, double, double, int, int);
    using FnDeleteObject   = int     (*)(int64_t, int, int);
    using FnAddPage        = int     (*)(int64_t);
    using FnSaveToFile     = int     (*)(int64_t, const char*);

    /* ============ 成员 ============ */
    void*       libHandle_ = nullptr;   // dlopen 返回值
    bool        loaded_    = false;

    FnOpenFile          fn_open_file_         = nullptr;
    FnCloseDoc          fn_close_doc_         = nullptr;
    FnGetPageCount      fn_get_page_count_    = nullptr;
    FnGetPageSize       fn_get_page_size_     = nullptr;
    FnGetLastError      fn_get_last_error_    = nullptr;
    FnFreeString        fn_free_string_       = nullptr;
    FnReadPageElems     fn_read_page_elements_ = nullptr;
    FnFreeElements      fn_free_elements_     = nullptr;
    FnModifyText        fn_modify_text_       = nullptr;
    FnAddText           fn_add_text_          = nullptr;
    FnAddRectPath       fn_add_rect_path_     = nullptr;
    FnDeleteObject      fn_delete_object_     = nullptr;
    FnAddPage           fn_add_page_          = nullptr;
    FnSaveToFile        fn_save_to_file_      = nullptr;

    /* ============ 辅助 ============ */
    /** dlsym 封装，找不到符号抛异常。 */
    template<typename Fn>
    Fn sym(const char* name);

    /** 获取 FFI 最后错误，抛异常。 */
    [[noreturn]] void throwLastError(int code, const std::string& prefix);
};

} // namespace ofd
