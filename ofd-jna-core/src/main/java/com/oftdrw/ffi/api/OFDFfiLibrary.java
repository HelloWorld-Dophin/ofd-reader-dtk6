package com.oftdrw.ffi.api;

import com.sun.jna.Library;
import com.sun.jna.Native;
import com.sun.jna.Pointer;

/**
 * OFD 文档 FFI 函数接口（C 风格 ABI）。
 *
 * <p>所有函数以 long 作为文档句柄，不暴露任何原始 Java 对象。
 * 通过 JNA 将该接口与本地/注册的符号进行绑定，供 C/C++/其他语言跨 FFI 调用。</p>
 *
 * <p>内存约定：</p>
 * <ul>
 *   <li>句柄由 {@code ofd_open_file} 分配，必须由 {@code ofd_close_doc} 释放；</li>
 *   <li>{@code ofd_get_last_error} 返回的字符串为 FFI 分配内存，
 *       必须由 {@code ofd_free_string} 释放，严防内存泄漏。</li>
 * </ul>
 */
public interface OFDFfiLibrary extends Library {

    OFDFfiLibrary INSTANCE = Native.load("ofd-jna-core", OFDFfiLibrary.class);

    /**
     * 打开 OFD 文档。
     *
     * @param path 文件路径字符串（UTF-8）
     * @return 文档句柄；打开失败返回 0（{@link com.oftdrw.ffi.handle.OFDHandleManager#INVALID_HANDLE}）
     */
    long ofd_open_file(String path);

    /**
     * 关闭文档，释放全部资源。
     *
     * @param handle 文档句柄
     * @return 0 表示成功，非 0 表示失败（句柄无效等）
     */
    int ofd_close_doc(long handle);

    /**
     * 获取文档总页面数量。
     *
     * @param handle 文档句柄
     * @return 页面数量；失败返回 -1
     */
    int ofd_get_page_count(long handle);

    /**
     * 获取最后一次错误信息字符串。
     * 返回值为 FFI 分配内存，调用后必须用 ofd_free_string 释放。
     *
     * @return 错误信息 UTF-8 字符串指针，无错误返回空串
     */
    Pointer ofd_get_last_error();

    /**
     * 释放 FFI 分配的错误字符串内存。
     *
     * @param p 由 ofd_get_last_error 返回的字符串指针；NULL 安全
     */
    void ofd_free_string(Pointer p);

    /**
     * 解析指定页面，返回页面全部元素（文本/图片/路径）集合容器指针。
     * 返回值为 FFI 分配内存，使用后必须用 ofd_free_elements 释放。
     *
     * @param handle    文档句柄
     * @param pageIndex 页面索引（0 起始）
     * @return 元素容器指针；解析失败或句柄无效返回 NULL
     */
    Pointer ofd_read_page_elements(long handle, int pageIndex);

    /**
     * 释放页面元素容器及其全部内部内存。
     *
     * @param elements 由 ofd_read_page_elements 返回的容器指针；NULL 安全
     */
    void ofd_free_elements(Pointer elements);

    /* ==================== 编辑能力 ==================== */

    /**
     * 修改已有文本对象。
     *
     * @param handle        文档句柄
     * @param pageIdx       页面索引（0 起始）
     * @param textItemIndex 文本对象索引（0 起始，与读取接口一致）
     * @param newText       新文本内容（UTF-8）
     * @param fontSize      新字号（&gt;0 时生效）
     * @param colorARGB     新颜色（0xAARRGGBB）
     * @return 成功返回 1；失败返回 0
     */
    int ofd_modify_text(long handle, int pageIdx, int textItemIndex,
                        String newText, double fontSize, int colorARGB);

    /**
     * 新增文本对象。
     *
     * @param handle   文档句柄
     * @param pageIdx  页面索引（0 起始）
     * @param x        文本起点 X（毫米）
     * @param y        文本起点 Y（毫米）
     * @param text     文本内容（UTF-8）
     * @param fontSize 字号（毫米）
     * @param colorARGB 颜色（0xAARRGGBB）
     * @return 成功返回 1；失败返回 0
     */
    int ofd_add_text(long handle, int pageIdx, double x, double y,
                     String text, double fontSize, int colorARGB);

    /**
     * 新增矩形矢量图形（PathObject）。
     *
     * @param handle      文档句柄
     * @param pageIdx     页面索引（0 起始）
     * @param x, y        矩形左上角坐标（毫米）
     * @param w, h        矩形宽高（毫米）
     * @param strokeWidth 描边宽度（毫米）
     * @param strokeColor 描边颜色（0xAARRGGBB）
     * @param fillColor   填充颜色（0xAARRGGBB）
     * @return 成功返回 1；失败返回 0
     */
    int ofd_add_rect_path(long handle, int pageIdx, double x, double y,
                          double w, double h, double strokeWidth,
                          int strokeColor, int fillColor);

    /**
     * 删除页面指定索引元素。
     *
     * @param handle   文档句柄
     * @param pageIdx  页面索引（0 起始）
     * @param objIndex 元素索引（0 起始，扁平化，与读取接口一致）
     * @return 成功返回 1；失败返回 0
     */
    int ofd_delete_object(long handle, int pageIdx, int objIndex);

    /**
     * 新增空白页。
     *
     * @param handle 文档句柄
     * @return 成功返回 1；失败返回 0
     */
    int ofd_add_page(long handle);

    /**
     * 将内存 DOM 另存为标准 OFD 文件（ofdrw 完成 zip 打包）。
     *
     * @param handle     文档句柄
     * @param outputPath 输出 OFD 文件路径（UTF-8）
     * @return 成功返回 1；失败返回 0
     */
    int ofd_save_to_file(long handle, String outputPath);
}