/*
 * ofd_jna_bridge.cpp — OFD JNA FFI C++ 封装实现
 *
 * 使用 POSIX dlopen/dlsym 动态加载 libofd-jna-core.so，
 * 对 JNA 侧的 13 个 C ABI 函数指针逐一解析并封装。
 */

#include "ofd_jna_bridge.h"

#include <dlfcn.h>
#include <cstring>
#include <cstdio>

namespace ofd {

/* ==================== 动态库加载辅助 ==================== */

template<typename Fn>
Fn OfdJnaBridge::sym(const char* name) {
    // 优先 C++ mangled，再尝试 unmangled（ofd_ 开头是纯 C 符号）
    void* p = ::dlsym(libHandle_, name);
    if (!p) {
        std::string msg = std::string("找不到符号 ") + name + ": " + ::dlerror();
        throw OfdException(-1, msg);
    }
    return reinterpret_cast<Fn>(p);
}

[[noreturn]]
void OfdJnaBridge::throwLastError(int code, const std::string& prefix) {
    std::string errMsg = prefix;
    if (fn_get_last_error_) {
        void* errPtr = fn_get_last_error_();
        if (errPtr) {
            const char* cstr = static_cast<const char*>(errPtr);
            if (cstr && cstr[0]) {
                errMsg += " | FFI: ";
                errMsg += cstr;
            }
            fn_free_string_(errPtr);
        }
    }
    throw OfdException(code, errMsg);
}

/* ==================== OfdPageElementsGuard ==================== */

OfdPageElementsGuard::~OfdPageElementsGuard() {
    if (raw_) {
        // 需要通过 OfdJnaBridge 调用 free，但我们没有 bridge 指针。
        // 简化：全局静态持有一次加载的 free_elements 函数指针。
        // 这样析构时不需要 bridge 也能释放。
        static void (*g_freeElements)(void*) = nullptr;
        if (!g_freeElements) {
            // 延迟加载：尝试 dlopen 自身进程获取函数指针
            void* h = ::dlopen(nullptr, RTLD_LAZY);
            if (h) {
                g_freeElements = reinterpret_cast<void (*)(void*)>(
                    ::dlsym(h, "ofd_free_elements"));
                ::dlclose(h);
            }
        }
        if (g_freeElements) {
            g_freeElements(raw_);
        }
        raw_ = nullptr;
    }
}

OfdPageElementsGuard::OfdPageElementsGuard(OfdPageElementsGuard&& other) noexcept
    : raw_(other.raw_) {
    other.raw_ = nullptr;
}

OfdPageElementsGuard& OfdPageElementsGuard::operator=(OfdPageElementsGuard&& other) noexcept {
    if (this != &other) {
        if (raw_) OfdPageElementsGuard::~OfdPageElementsGuard();
        raw_ = other.raw_;
        other.raw_ = nullptr;
    }
    return *this;
}

/* ==================== OfdJnaBridge ==================== */

OfdJnaBridge::OfdJnaBridge(const std::string& libPath) {
    // 尝试带 lib 前缀的几种常见形式
    std::string path;
    if (libPath.find('/') != std::string::npos) {
        // 包含路径，直接用
        path = libPath;
    } else if (libPath.find("lib") == 0) {
        // 已经带 lib 前缀
        path = libPath;
    } else {
        path = "lib" + libPath + ".so";
    }

    libHandle_ = ::dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!libHandle_) {
        // 再尝试不带 lib 前缀的原始名
        libHandle_ = ::dlopen(libPath.c_str(), RTLD_NOW | RTLD_LOCAL);
    }
    if (!libHandle_) {
        throw OfdException(-1,
            std::string("加载 JNA 动态库失败: ") + path + " | " + ::dlerror() +
            "\n提示: 请确保 libofd-jna-core.so 在 LD_LIBRARY_PATH 或系统库目录中");
    }

    // 解析所有 13 个函数指针
    fn_open_file_          = sym<FnOpenFile>        ("ofd_open_file");
    fn_close_doc_          = sym<FnCloseDoc>        ("ofd_close_doc");
    fn_get_page_count_     = sym<FnGetPageCount>    ("ofd_get_page_count");
    fn_get_page_size_      = sym<FnGetPageSize>     ("ofd_get_page_size");
    fn_get_last_error_     = sym<FnGetLastError>    ("ofd_get_last_error");
    fn_free_string_        = sym<FnFreeString>      ("ofd_free_string");
    fn_read_page_elements_ = sym<FnReadPageElems>   ("ofd_read_page_elements");
    fn_free_elements_      = sym<FnFreeElements>    ("ofd_free_elements");
    fn_modify_text_        = sym<FnModifyText>      ("ofd_modify_text");
    fn_add_text_           = sym<FnAddText>         ("ofd_add_text");
    fn_add_rect_path_      = sym<FnAddRectPath>     ("ofd_add_rect_path");
    fn_delete_object_      = sym<FnDeleteObject>    ("ofd_delete_object");
    fn_add_page_           = sym<FnAddPage>         ("ofd_add_page");
    fn_save_to_file_       = sym<FnSaveToFile>      ("ofd_save_to_file");
    fn_get_work_dir_       = sym<FnGetWorkDir>      ("ofd_get_work_dir");

    loaded_ = true;
}

OfdJnaBridge::~OfdJnaBridge() {
    if (libHandle_) {
        ::dlclose(libHandle_);
        libHandle_ = nullptr;
    }
}

OfdJnaBridge::OfdJnaBridge(OfdJnaBridge&& other) noexcept
    : libHandle_(other.libHandle_), loaded_(other.loaded_),
      fn_open_file_(other.fn_open_file_),
      fn_close_doc_(other.fn_close_doc_),
      fn_get_page_count_(other.fn_get_page_count_),
      fn_get_page_size_(other.fn_get_page_size_),
      fn_get_last_error_(other.fn_get_last_error_),
      fn_free_string_(other.fn_free_string_),
      fn_read_page_elements_(other.fn_read_page_elements_),
      fn_free_elements_(other.fn_free_elements_),
      fn_modify_text_(other.fn_modify_text_),
      fn_add_text_(other.fn_add_text_),
      fn_add_rect_path_(other.fn_add_rect_path_),
      fn_delete_object_(other.fn_delete_object_),
      fn_add_page_(other.fn_add_page_),
      fn_save_to_file_(other.fn_save_to_file_),
      fn_get_work_dir_(other.fn_get_work_dir_) {
    other.libHandle_ = nullptr;
    other.loaded_ = false;
}

OfdJnaBridge& OfdJnaBridge::operator=(OfdJnaBridge&& other) noexcept {
    if (this != &other) {
        if (libHandle_) ::dlclose(libHandle_);
        libHandle_          = other.libHandle_;
        loaded_             = other.loaded_;
        fn_open_file_       = other.fn_open_file_;
        fn_close_doc_       = other.fn_close_doc_;
        fn_get_page_count_  = other.fn_get_page_count_;
        fn_get_page_size_   = other.fn_get_page_size_;
        fn_get_last_error_  = other.fn_get_last_error_;
        fn_free_string_     = other.fn_free_string_;
        fn_read_page_elements_ = other.fn_read_page_elements_;
        fn_free_elements_   = other.fn_free_elements_;
        fn_modify_text_     = other.fn_modify_text_;
        fn_add_text_        = other.fn_add_text_;
        fn_add_rect_path_   = other.fn_add_rect_path_;
        fn_delete_object_   = other.fn_delete_object_;
        fn_add_page_        = other.fn_add_page_;
        fn_save_to_file_    = other.fn_save_to_file_;
        fn_get_work_dir_    = other.fn_get_work_dir_;
        other.libHandle_ = nullptr;
        other.loaded_ = false;
    }
    return *this;
}

OfdJnaBridge::Doc OfdJnaBridge::open(const std::string& path) {
    int64_t handle = fn_open_file_(path.c_str());
    if (handle == 0) {
        throwLastError(0, "打开 OFD 文档失败: " + path);
    }
    return Doc(this, handle);
}

/* ==================== Doc RAII 句柄 ==================== */

OfdJnaBridge::Doc::~Doc() {
    if (handle_ && bridge_ && bridge_->fn_close_doc_) {
        bridge_->fn_close_doc_(handle_);
    }
}

OfdJnaBridge::Doc::Doc(OfdJnaBridge::Doc&& other) noexcept
    : bridge_(other.bridge_), handle_(other.handle_) {
    other.bridge_ = nullptr;
    other.handle_ = 0;
}

OfdJnaBridge::Doc& OfdJnaBridge::Doc::operator=(OfdJnaBridge::Doc&& other) noexcept {
    if (this != &other) {
        if (handle_ && bridge_ && bridge_->fn_close_doc_) {
            bridge_->fn_close_doc_(handle_);
        }
        bridge_ = other.bridge_;
        handle_ = other.handle_;
        other.bridge_ = nullptr;
        other.handle_ = 0;
    }
    return *this;
}

int OfdJnaBridge::Doc::pageCount() const {
    if (!handle_ || !bridge_) throw OfdException(-1, "文档句柄无效");
    int n = bridge_->fn_get_page_count_(handle_);
    if (n < 0) {
        bridge_->throwLastError(n, "获取页面数量失败");
    }
    return n;
}

OfdPageSize OfdJnaBridge::Doc::pageSize(int pageIndex) const {
    if (!handle_ || !bridge_) throw OfdException(-1, "文档句柄无效");
    OfdPageSize sz;
    int ok = bridge_->fn_get_page_size_(handle_, pageIndex, &sz.x, &sz.y, &sz.width, &sz.height);
    if (ok != 1) {
        bridge_->throwLastError(ok, "获取页面尺寸失败");
    }
    return sz;
}

OfdPageElementsGuard OfdJnaBridge::Doc::readPage(int pageIndex) const {
    if (!handle_ || !bridge_) throw OfdException(-1, "文档句柄无效");
    void* raw = bridge_->fn_read_page_elements_(handle_, pageIndex);
    if (!raw) {
        bridge_->throwLastError(-1, "读取页面元素失败 (page=" + std::to_string(pageIndex) + ")");
    }
    return OfdPageElementsGuard(raw);
}

void OfdJnaBridge::Doc::modifyText(int pageIdx, int textItemIndex,
                                    const std::string& newText,
                                    double fontSize, int32_t colorARGB) {
    if (!handle_ || !bridge_) throw OfdException(-1, "文档句柄无效");
    int ok = bridge_->fn_modify_text_(handle_, pageIdx, textItemIndex,
                                       newText.c_str(), fontSize, colorARGB);
    if (ok != 1) {
        bridge_->throwLastError(ok, "modifyText 失败");
    }
}

void OfdJnaBridge::Doc::addText(int pageIdx, double x, double y,
                                 const std::string& text,
                                 double fontSize, int32_t colorARGB) {
    if (!handle_ || !bridge_) throw OfdException(-1, "文档句柄无效");
    int ok = bridge_->fn_add_text_(handle_, pageIdx, x, y,
                                   text.c_str(), fontSize, colorARGB);
    if (ok != 1) {
        bridge_->throwLastError(ok, "addText 失败");
    }
}

void OfdJnaBridge::Doc::addRectPath(int pageIdx, double x, double y,
                                     double w, double h, double strokeWidth,
                                     int32_t strokeColor, int32_t fillColor) {
    if (!handle_ || !bridge_) throw OfdException(-1, "文档句柄无效");
    int ok = bridge_->fn_add_rect_path_(handle_, pageIdx, x, y, w, h, strokeWidth,
                                        strokeColor, fillColor);
    if (ok != 1) {
        bridge_->throwLastError(ok, "addRectPath 失败");
    }
}

void OfdJnaBridge::Doc::deleteObject(int pageIdx, int objIndex) {
    if (!handle_ || !bridge_) throw OfdException(-1, "文档句柄无效");
    int ok = bridge_->fn_delete_object_(handle_, pageIdx, objIndex);
    if (ok != 1) {
        bridge_->throwLastError(ok, "deleteObject 失败");
    }
}

void OfdJnaBridge::Doc::addPage() {
    if (!handle_ || !bridge_) throw OfdException(-1, "文档句柄无效");
    int ok = bridge_->fn_add_page_(handle_);
    if (ok != 1) {
        bridge_->throwLastError(ok, "addPage 失败");
    }
}

void OfdJnaBridge::Doc::saveAs(const std::string& outputPath) {
    if (!handle_ || !bridge_) throw OfdException(-1, "文档句柄无效");
    int ok = bridge_->fn_save_to_file_(handle_, outputPath.c_str());
    if (ok != 1) {
        bridge_->throwLastError(ok, "saveAs 失败: " + outputPath);
    }
}

std::string OfdJnaBridge::Doc::workDir() const {
    if (!handle_ || !bridge_) throw OfdException(-1, "文档句柄无效");
    void* raw = bridge_->fn_get_work_dir_(handle_);
    if (!raw) return {};
    std::string result(static_cast<const char*>(raw));
    bridge_->fn_free_string_(raw);
    return result;
}

} // namespace ofd
