package com.oftdrw.ffi.impl;

import java.nio.file.Path;
import java.nio.file.Paths;

import org.ofdrw.reader.OFDReader;

import com.sun.jna.Pointer;

import com.oftdrw.ffi.api.OFDErrorBuffer;
import com.oftdrw.ffi.api.OfdStructs.OfdPageElements;
import com.oftdrw.ffi.handle.OFDHandleManager;

/**
 * OFD 文档 FFI 核心实现。
 *
 * <p>本阶段仅实现：打开、关闭、获取页数、错误串与字符串释放。
 * 所有对外函数均以 long 句柄交互，不暴露原始 Java 对象。</p>
 *
 * <p>全部逻辑包裹异常捕获，异常信息写入错误缓冲区 {@link OFDErrorBuffer}，
 * 供 {@code ofd_get_last_error} 读取。</p>
 */
public final class OFDFfi {

    private OFDFfi() {
        // 工具类，禁止实例化
    }

    /**
     * 打开 OFD 文档。
     *
     * @param path 文件路径字符串
     * @return 文档句柄；打开失败返回 0
     */
    public static long ofd_open_file(String path) {
        try {
            if (path == null || path.trim().isEmpty()) {
                OFDErrorBuffer.setError("ofd_open_file: 文件路径为空");
                return OFDHandleManager.INVALID_HANDLE;
            }
            Path file = Paths.get(path);
            if (!java.nio.file.Files.exists(file)) {
                OFDErrorBuffer.setError("ofd_open_file: 文件不存在: " + path);
                return OFDHandleManager.INVALID_HANDLE;
            }
            OFDReader reader = new OFDReader(file);
            return OFDHandleManager.register(reader);
        } catch (Throwable t) {
            OFDErrorBuffer.setError("ofd_open_file 异常: " + t.getMessage());
            return OFDHandleManager.INVALID_HANDLE;
        }
    }

    /**
     * 关闭文档并释放全部资源。
     *
     * @param handle 文档句柄
     * @return 0 成功；非 0 失败
     */
    public static int ofd_close_doc(long handle) {
        try {
            boolean ok = OFDHandleManager.free(handle);
            if (!ok) {
                OFDErrorBuffer.setError("ofd_close_doc: 句柄无效或已释放: " + handle);
                return -1;
            }
            return 0;
        } catch (Throwable t) {
            OFDErrorBuffer.setError("ofd_close_doc 异常: " + t.getMessage());
            return -1;
        }
    }

    /**
     * 获取文档总页面数量。
     *
     * @param handle 文档句柄
     * @return 页面数量；失败返回 -1
     */
    public static int ofd_get_page_count(long handle) {
        try {
            OFDReader reader = OFDHandleManager.get(handle);
            if (reader == null) {
                OFDErrorBuffer.setError("ofd_get_page_count: 句柄无效或未打开: " + handle);
                return -1;
            }
            return reader.getNumberOfPages();
        } catch (Throwable t) {
            OFDErrorBuffer.setError("ofd_get_page_count 异常: " + t.getMessage());
            return -1;
        }
    }

    /**
     * 获取指定页面的物理盒子（PhysicalBox）。
     *
     * <p>OFD 物理坐标原点在左上角，单位 mm。输出顺序：x, y, width, height。</p>
     *
     * @param handle    文档句柄
     * @param pageIndex 页面索引（0 起始）
     * @param outXYWH   长度 ≥4 的 double 数组；成功时写入 [x, y, width, height]
     * @return 成功返回 1；失败返回 0
     */
    public static int ofd_get_page_size(long handle, int pageIndex, double[] outXYWH) {
        try {
            OFDReader reader = OFDHandleManager.get(handle);
            if (reader == null) {
                OFDErrorBuffer.setError("ofd_get_page_size: 句柄无效");
                return 0;
            }
            if (outXYWH == null || outXYWH.length < 4) {
                OFDErrorBuffer.setError("ofd_get_page_size: 输出数组不足 4 格");
                return 0;
            }
            // reader.getPageSize 页码 1 起始
            org.ofdrw.core.basicType.ST_Box box = reader.getPageSize(pageIndex + 1);
            if (box == null) {
                OFDErrorBuffer.setError("ofd_get_page_size: 页面无尺寸");
                return 0;
            }
            double x = box.getTopLeftX() != null ? box.getTopLeftX() : 0.0;
            double y = box.getTopLeftY() != null ? box.getTopLeftY() : 0.0;
            double w = box.getWidth() != null ? box.getWidth() : 210.0;   // A4 默认
            double h = box.getHeight() != null ? box.getHeight() : 297.0;
            System.err.printf("[getPageSize] pageIdx=%d -> box=(%.1f,%.1f %.1fx%.1f)%n", pageIndex + 1, x, y, w, h);
            outXYWH[0] = x;
            outXYWH[1] = y;
            outXYWH[2] = w;
            outXYWH[3] = h;
            return 1;
        } catch (Throwable t) {
            OFDErrorBuffer.setError("ofd_get_page_size 异常: " + t.getMessage());
            return 0;
        }
    }

    /**
     * 获取最后一次错误信息字符串。
     *
     * @return 错误信息（UTF-8），无错误返回空串
     */
    public static String ofd_get_last_error() {
        return OFDErrorBuffer.getError();
    }

    /**
     * 解析指定页面，返回页面全部元素容器（文本/图片/路径集合）的 FFI 指针。
     *
     * <p>容器内存为 FFI 分配，使用后必须调用 {@code ofd_free_elements} 释放，严防内存泄漏。</p>
     *
     * @param handle    文档句柄
     * @param pageIndex 页面索引（0 起始）
     * @return 元素容器指针；解析失败或句柄无效时返回 NULL
     */
    public static Pointer ofd_read_page_elements(long handle, int pageIndex) {
        try {
            OFDReader reader = OFDHandleManager.get(handle);
            if (reader == null) {
                OFDErrorBuffer.setError("ofd_read_page_elements: 句柄无效或未打开: " + handle);
                return null;
            }
            if (pageIndex < 0) {
                OFDErrorBuffer.setError("ofd_read_page_elements: 页面索引非法: " + pageIndex);
                return null;
            }
            Pointer elems = OFDPageParser.parsePage(reader, pageIndex);
            if (elems == null) {
                // parsePage 已写入真实异常到错误缓冲，此处不再覆盖
                if (OFDErrorBuffer.getError() == null
                        || OFDErrorBuffer.getError().isEmpty()) {
                    OFDErrorBuffer.setError("ofd_read_page_elements: 解析页面失败: " + pageIndex);
                }
                return null;
            }
            return elems;
        } catch (Throwable t) {
            OFDErrorBuffer.setError("ofd_read_page_elements 异常: " + t.getMessage());
            return null;
        }
    }

    /**
     * 释放页面元素容器及其全部内部内存。
     *
     * @param elements 由 ofd_read_page_elements 返回的容器指针；NULL 安全
     */
    public static void ofd_free_elements(Pointer elements) {
        try {
            OFDPageParser.freeElements(elements);
        } catch (Throwable t) {
            OFDErrorBuffer.setError("ofd_free_elements 异常: " + t.getMessage());
        }
    }

    /* ==================== 编辑能力（基于 ofdrw 内存 DOM） ==================== */

    /**
     * 修改已有文本对象。
     *
     * @param handle        文档句柄
     * @param pageIdx       页面索引（0 起始）
     * @param textItemIndex 文本对象索引（0 起始，与读取接口一致）
     * @param newText       新文本内容
     * @param fontSize      新字号（&gt;0 时生效）
     * @param colorARGB     新颜色（0xAARRGGBB）
     * @return 成功返回 1；失败返回 0
     */
    public static int ofd_modify_text(long handle, int pageIdx, int textItemIndex,
                                      String newText, double fontSize, int colorARGB) {
        try {
            OFDReader reader = OFDHandleManager.get(handle);
            if (reader == null) {
                OFDErrorBuffer.setError("ofd_modify_text: 句柄无效或未打开: " + handle);
                return 0;
            }
            return OFDDocumentEditor.modifyText(reader, pageIdx, textItemIndex,
                    newText, fontSize, colorARGB);
        } catch (Throwable t) {
            OFDErrorBuffer.setError("ofd_modify_text 异常: " + t.getMessage());
            return 0;
        }
    }

    /**
     * 新增文本对象。
     *
     * @return 成功返回 1；失败返回 0
     */
    public static int ofd_add_text(long handle, int pageIdx, double x, double y,
                                   String text, double fontSize, int colorARGB) {
        try {
            OFDReader reader = OFDHandleManager.get(handle);
            if (reader == null) {
                OFDErrorBuffer.setError("ofd_add_text: 句柄无效或未打开: " + handle);
                return 0;
            }
            return OFDDocumentEditor.addText(reader, pageIdx, x, y, text, fontSize, colorARGB);
        } catch (Throwable t) {
            OFDErrorBuffer.setError("ofd_add_text 异常: " + t.getMessage());
            return 0;
        }
    }

    /**
     * 新增矩形矢量图形（PathObject）。
     *
     * @return 成功返回 1；失败返回 0
     */
    public static int ofd_add_rect_path(long handle, int pageIdx, double x, double y,
                                        double w, double h, double strokeWidth,
                                        int strokeColor, int fillColor) {
        try {
            OFDReader reader = OFDHandleManager.get(handle);
            if (reader == null) {
                OFDErrorBuffer.setError("ofd_add_rect_path: 句柄无效或未打开: " + handle);
                return 0;
            }
            return OFDDocumentEditor.addRectPath(reader, pageIdx, x, y, w, h,
                    strokeWidth, strokeColor, fillColor);
        } catch (Throwable t) {
            OFDErrorBuffer.setError("ofd_add_rect_path 异常: " + t.getMessage());
            return 0;
        }
    }

    /**
     * 删除页面指定索引元素。
     *
     * @return 成功返回 1；失败返回 0
     */
    public static int ofd_delete_object(long handle, int pageIdx, int objIndex) {
        try {
            OFDReader reader = OFDHandleManager.get(handle);
            if (reader == null) {
                OFDErrorBuffer.setError("ofd_delete_object: 句柄无效或未打开: " + handle);
                return 0;
            }
            return OFDDocumentEditor.deleteObject(reader, pageIdx, objIndex);
        } catch (Throwable t) {
            OFDErrorBuffer.setError("ofd_delete_object 异常: " + t.getMessage());
            return 0;
        }
    }

    /**
     * 新增空白页。
     *
     * @return 成功返回 1；失败返回 0
     */
    public static int ofd_add_page(long handle) {
        try {
            OFDReader reader = OFDHandleManager.get(handle);
            if (reader == null) {
                OFDErrorBuffer.setError("ofd_add_page: 句柄无效或未打开: " + handle);
                return 0;
            }
            return OFDDocumentEditor.addPage(reader);
        } catch (Throwable t) {
            OFDErrorBuffer.setError("ofd_add_page 异常: " + t.getMessage());
            return 0;
        }
    }

    /**
     * 将内存 DOM 另存为标准 OFD 文件（ofdrw 完成 zip 打包）。
     *
     * @return 成功返回 1；失败返回 0
     */
    public static int ofd_save_to_file(long handle, String outputPath) {
        try {
            OFDReader reader = OFDHandleManager.get(handle);
            if (reader == null) {
                OFDErrorBuffer.setError("ofd_save_to_file: 句柄无效或未打开: " + handle);
                return 0;
            }
            return OFDDocumentEditor.saveToFile(reader, outputPath);
        } catch (Throwable t) {
            OFDErrorBuffer.setError("ofd_save_to_file 异常: " + t.getMessage());
            return 0;
        }
    }
}