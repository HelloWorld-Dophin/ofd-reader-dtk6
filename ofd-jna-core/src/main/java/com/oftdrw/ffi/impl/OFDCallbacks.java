package com.oftdrw.ffi.impl;

import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;

import com.sun.jna.Callback;
import com.sun.jna.CallbackReference;
import com.sun.jna.Native;
import com.sun.jna.Pointer;

import com.oftdrw.ffi.api.OFDErrorBuffer;
import com.oftdrw.ffi.api.OfdStructs.OfdPageElements;
import com.oftdrw.ffi.handle.OFDHandleManager;

/**
 * OFD FFI 回调桥接层。
 *
 * <p>通过 JNA {@link CallbackReference#getFunctionPointer(Callback)} 将 Java 实现的
 * C 风格 FFI 函数导出为原生函数指针（{@link Pointer}），供 C/C++/其他语言跨 FFI 调用。
 * 所有函数均以 long 句柄交互，不暴露原始 Java 对象。</p>
 *
 * <p>字符串内存约定：{@code ofd_get_last_error} 返回 {@link Pointer} 指向 FFI 分配的
 * UTF-8 字节数组（含结尾 NUL）。分配大小登记在 {@link #ALLOCATIONS} 表中，
 * 调用方必须在用完后调用 {@code ofd_free_string} 释放，严防内存泄漏。</p>
 */
public final class OFDCallbacks {

    private OFDCallbacks() {
        // 工具类，禁止实例化
    }

    /** 记录 FFI 分配的字符串指针 -> 分配大小，供 ofd_free_string 释放用。 */
    private static final Map<Pointer, Long> ALLOCATIONS = new ConcurrentHashMap<>();

    /** ofd_open_file 回调。 */
    public interface OpenFileCallback extends Callback {
        long invoke(String path);
    }

    /** ofd_close_doc 回调。 */
    public interface CloseDocCallback extends Callback {
        int invoke(long handle);
    }

    /** ofd_get_page_count 回调。 */
    public interface GetPageCountCallback extends Callback {
        int invoke(long handle);
    }

    /** ofd_get_last_error 回调，返回 FFI 分配的字符串指针。 */
    public interface GetLastErrorCallback extends Callback {
        Pointer invoke();
    }

    /** ofd_free_string 回调，释放 FFI 分配的字符串内存。 */
    public interface FreeStringCallback extends Callback {
        void invoke(Pointer p);
    }

    /** ofd_read_page_elements 回调，解析指定页面并返回元素容器指针。 */
    public interface ReadPageElementsCallback extends Callback {
        Pointer invoke(long handle, int pageIndex);
    }

    /** ofd_free_elements 回调，释放页面元素容器及其全部内部内存。 */
    public interface FreeElementsCallback extends Callback {
        void invoke(Pointer elements);
    }

    /* ==================== 编辑能力回调 ==================== */

    /** ofd_modify_text 回调：修改已有文本。 */
    public interface ModifyTextCallback extends Callback {
        int invoke(long handle, int pageIdx, int textItemIndex,
                   String newText, double fontSize, int colorARGB);
    }

    /** ofd_add_text 回调：新增文本。 */
    public interface AddTextCallback extends Callback {
        int invoke(long handle, int pageIdx, double x, double y,
                   String text, double fontSize, int colorARGB);
    }

    /** ofd_add_rect_path 回调：新增矩形矢量图形。 */
    public interface AddRectPathCallback extends Callback {
        int invoke(long handle, int pageIdx, double x, double y, double w, double h,
                   double strokeWidth, int strokeColor, int fillColor);
    }

    /** ofd_delete_object 回调：删除页面指定索引元素。 */
    public interface DeleteObjectCallback extends Callback {
        int invoke(long handle, int pageIdx, int objIndex);
    }

    /** ofd_add_page 回调：新增空白页。 */
    public interface AddPageCallback extends Callback {
        int invoke(long handle);
    }

    /** ofd_save_to_file 回调：另存为标准 OFD 文件。 */
    public interface SaveToFileCallback extends Callback {
        int invoke(long handle, String outputPath);
    }

    /** ofd_open_file 的 Java 实现。 */
    private static final OpenFileCallback OPEN_FILE = OFDFfi::ofd_open_file;

    /** ofd_close_doc 的 Java 实现。 */
    private static final CloseDocCallback CLOSE_DOC = OFDFfi::ofd_close_doc;

    /** ofd_get_page_count 的 Java 实现。 */
    private static final GetPageCountCallback PAGE_COUNT = OFDFfi::ofd_get_page_count;

    /** ofd_get_last_error 的 Java 实现：分配并登记错误串。 */
    private static final GetLastErrorCallback LAST_ERROR = () -> {
        byte[] bytes = OFDErrorBuffer.toByteArrayWithNul();
        // 使用裸指针分配，彻底绕开 JNA Memory 的 GC finalizer，避免二次释放
        Pointer nativeMem = new Pointer(Native.malloc(bytes.length));
        nativeMem.write(0, bytes, 0, bytes.length);
        ALLOCATIONS.put(nativeMem, (long) bytes.length);
        return nativeMem;
    };

    /** ofd_free_string 的 Java 实现：按登记大小释放。 */
    private static final FreeStringCallback FREE_STRING = p -> {
        if (p == null) {
            return;
        }
        Long size = ALLOCATIONS.remove(p);
        if (size != null) {
            Native.free(Pointer.nativeValue(p));
        }
    };

    /** ofd_read_page_elements 的 Java 实现。 */
    private static final ReadPageElementsCallback READ_PAGE_ELEMENTS = OFDFfi::ofd_read_page_elements;

    /** ofd_free_elements 的 Java 实现：释放页面元素容器。 */
    private static final FreeElementsCallback FREE_ELEMENTS = OFDPageParser::freeElements;

    /* ==================== 编辑能力实现 ==================== */

    /** ofd_modify_text 的 Java 实现。 */
    private static final ModifyTextCallback MODIFY_TEXT = OFDFfi::ofd_modify_text;

    /** ofd_add_text 的 Java 实现。 */
    private static final AddTextCallback ADD_TEXT = OFDFfi::ofd_add_text;

    /** ofd_add_rect_path 的 Java 实现。 */
    private static final AddRectPathCallback ADD_RECT_PATH = OFDFfi::ofd_add_rect_path;

    /** ofd_delete_object 的 Java 实现。 */
    private static final DeleteObjectCallback DELETE_OBJECT = OFDFfi::ofd_delete_object;

    /** ofd_add_page 的 Java 实现。 */
    private static final AddPageCallback ADD_PAGE = OFDFfi::ofd_add_page;

    /** ofd_save_to_file 的 Java 实现。 */
    private static final SaveToFileCallback SAVE_TO_FILE = OFDFfi::ofd_save_to_file;

    /**
     * 获取各 FFI 函数对应的原生函数指针。
     *
     * @return 长度为 13 的函数指针数组，顺序为：
     *         ofd_open_file, ofd_close_doc, ofd_get_page_count,
     *         ofd_get_last_error, ofd_free_string, ofd_read_page_elements, ofd_free_elements,
     *         ofd_modify_text, ofd_add_text, ofd_add_rect_path,
     *         ofd_delete_object, ofd_add_page, ofd_save_to_file
     */
    public static Pointer[] getFunctionPointers() {
        return new Pointer[]{
                CallbackReference.getFunctionPointer(OPEN_FILE),
                CallbackReference.getFunctionPointer(CLOSE_DOC),
                CallbackReference.getFunctionPointer(PAGE_COUNT),
                CallbackReference.getFunctionPointer(LAST_ERROR),
                CallbackReference.getFunctionPointer(FREE_STRING),
                CallbackReference.getFunctionPointer(READ_PAGE_ELEMENTS),
                CallbackReference.getFunctionPointer(FREE_ELEMENTS),
                CallbackReference.getFunctionPointer(MODIFY_TEXT),
                CallbackReference.getFunctionPointer(ADD_TEXT),
                CallbackReference.getFunctionPointer(ADD_RECT_PATH),
                CallbackReference.getFunctionPointer(DELETE_OBJECT),
                CallbackReference.getFunctionPointer(ADD_PAGE),
                CallbackReference.getFunctionPointer(SAVE_TO_FILE)
        };
    }

    /** 当前存活句柄数（自检用）。 */
    public static int activeHandleCount() {
        return OFDHandleManager.activeCount();
    }
}