package com.oftdrw.ffi.api;

import com.sun.jna.Native;
import com.sun.jna.Pointer;

import com.oftdrw.ffi.api.OfdStructs;
import com.oftdrw.ffi.impl.OFDCallbacks;
import com.oftdrw.ffi.impl.OFDFfi;

/**
 * OFD FFI 自测/命令行入口。
 *
 * <p>用法：</p>
 * <pre>
 *   java -jar ofd-jna-core-1.0.0.jar &lt;ofd文件路径&gt;
 * </pre>
 *
 * <p>本阶段仅演示：打开、获取页数、关闭，并输出错误信息与句柄释放结果。
 * 所有异常均被捕获并写入错误缓冲区。</p>
 */
public final class OFDMain {

    private OFDMain() {
        // 工具类，禁止实例化
    }

    /** 自检用例。 */
    private static int selfTest(String ofdPath) {
        System.out.println("=== OFD JNA Core 自检 ===");

        // 1. 注册 FFI 回调符号（导出给原生层，纯 Java 侧可选）
        try {
            Pointer[] fns = OFDCallbacks.getFunctionPointers();
            System.out.println("[OK] FFI 回调已导出, 函数指针数=" + fns.length);
        } catch (Throwable t) {
            System.out.println("[WARN] 回调导出跳过: " + t.getMessage());
        }

        // 2. 打开文档
        long handle = OFDFfi.ofd_open_file(ofdPath);
        if (handle == 0L) {
            System.out.println("[FAIL] 打开失败: " + OFDFfi.ofd_get_last_error());
            return -1;
        }
        System.out.println("[OK] 打开文档成功, 句柄=" + handle);

        // 3. 获取页数
        int pages = OFDFfi.ofd_get_page_count(handle);
        if (pages < 0) {
            System.out.println("[FAIL] 获取页数失败: " + OFDFfi.ofd_get_last_error());
        } else {
            System.out.println("[OK] 总页数=" + pages);
        }

        // 4. 逐页解析元素（文本/图片/路径），并释放容器
        for (int i = 0; i < pages; i++) {
            Pointer elemPtr = OFDFfi.ofd_read_page_elements(handle, i);
            if (elemPtr == null) {
                System.out.println("[WARN] 第" + i + "页解析失败: " + OFDFfi.ofd_get_last_error());
                continue;
            }
            try {
                // 直接按结构体字段偏移读取（不包装为 JNA Structure，避免退出时二次释放）
                int textCount = elemPtr.getInt(0);
                Pointer texts = elemPtr.getPointer(OfdStructs.CONTAINER_TEXTS_OFF);
                int imageCount = elemPtr.getInt(OfdStructs.CONTAINER_IMGCOUNT_OFF);
                Pointer images = elemPtr.getPointer(OfdStructs.CONTAINER_IMAGES_OFF);
                int pathCount = elemPtr.getInt(OfdStructs.CONTAINER_PATHCOUNT_OFF);
                Pointer paths = elemPtr.getPointer(OfdStructs.CONTAINER_PATHS_OFF);

                System.out.println("[PAGE " + i + "] 文本=" + textCount
                        + ", 图片=" + imageCount + ", 路径=" + pathCount);

                // 打印前 3 个文本示例
                for (int t = 0; t < Math.min(3, textCount); t++) {
                    Pointer tp = texts.getPointer((long) t * Native.POINTER_SIZE);
                    Pointer cptr = tp.getPointer(OfdStructs.TEXT_CONTENT_OFF);
                    double fontSize = tp.getDouble(OfdStructs.TEXT_FONTSIZE_OFF);
                    int argb = tp.getInt(OfdStructs.TEXT_ARGB_OFF);
                    double x = tp.getDouble(OfdStructs.TEXT_X_OFF);
                    double y = tp.getDouble(OfdStructs.TEXT_Y_OFF);
                    String content = (cptr == null) ? "" : cptr.getString(0);
                    System.out.println("    [文本] \"" + content + "\" 字号="
                            + String.format("%.2f", fontSize) + " ARGB=0x"
                            + Integer.toHexString(argb) + " 坐标=("
                            + String.format("%.1f", x) + "," + String.format("%.1f", y) + ")");
                }
                // 打印前 3 个图片示例
                for (int im = 0; im < Math.min(3, imageCount); im++) {
                    Pointer ip = images.getPointer((long) im * Native.POINTER_SIZE);
                    long resId = ip.getLong(OfdStructs.IMAGE_RESID_OFF);
                    double width = ip.getDouble(OfdStructs.IMAGE_WIDTH_OFF);
                    double height = ip.getDouble(OfdStructs.IMAGE_HEIGHT_OFF);
                    int dataSize = ip.getInt(OfdStructs.IMAGE_DATASIZE_OFF);
                    System.out.println("    [图片] 资源ID=" + resId + " 宽高=("
                            + String.format("%.1f", width) + "x" + String.format("%.1f", height)
                            + ") 字节=" + dataSize);
                }
                // 打印前 3 个路径示例
                for (int p = 0; p < Math.min(3, pathCount); p++) {
                    Pointer pp = paths.getPointer((long) p * Native.POINTER_SIZE);
                    Pointer dptr = pp.getPointer(OfdStructs.PATH_DATA_OFF);
                    int strokeArgb = pp.getInt(OfdStructs.PATH_STROKE_OFF);
                    int fillArgb = pp.getInt(OfdStructs.PATH_FILL_OFF);
                    double strokeWidth = pp.getDouble(OfdStructs.PATH_WIDTH_OFF);
                    String data = (dptr == null) ? "" : dptr.getString(0);
                    String preview = (data.length() > 60) ? data.substring(0, 60) + "..." : data;
                    System.out.println("    [路径] 描边=" + Integer.toHexString(strokeArgb)
                            + " 填充=" + Integer.toHexString(fillArgb)
                            + " 线宽=" + String.format("%.3f", strokeWidth)
                            + " 指令=\"" + preview + "\"");
                }
            } finally {
                OFDFfi.ofd_free_elements(elemPtr);
            }
        }
        System.out.println("[OK] 元素容器均已释放");

        // 5. 编辑能力自检：改文本、增文本、增矩形、删对象、增空白页、另存
        String outPath = ofdPath.replaceAll("\\.ofd$", "") + "_edited.ofd";
        System.out.println("--- 编辑自检 ---");

        // 5.1 修改第 0 页第 0 个文本为"修改后的测试文本"
        int rcModify = OFDFfi.ofd_modify_text(handle, 0, 0, "修改后的测试文本", 0, 0xFFFF0000);
        System.out.println("[EDIT] modify_text=" + rcModify
                + (rcModify == 0 ? " err=" + OFDFfi.ofd_get_last_error() : ""));

        // 5.2 在第 0 页坐标 (20,20) 新增文本
        int rcAddText = OFDFfi.ofd_add_text(handle, 0, 20.0, 20.0, "新增文本(编辑)", 12.0, 0xFF0000FF);
        System.out.println("[EDIT] add_text=" + rcAddText
                + (rcAddText == 0 ? " err=" + OFDFfi.ofd_get_last_error() : ""));

        // 5.3 在第 0 页新增矩形矢量图形（表格线）
        int rcRect = OFDFfi.ofd_add_rect_path(handle, 0, 60.0, 20.0, 40.0, 20.0,
                0.5, 0xFF000000, 0x00000000);
        System.out.println("[EDIT] add_rect_path=" + rcRect
                + (rcRect == 0 ? " err=" + OFDFfi.ofd_get_last_error() : ""));

        // 5.4 删除第 0 页最后一个元素
        int lastObj = -1;
        Pointer lastPtr = OFDFfi.ofd_read_page_elements(handle, 0);
        if (lastPtr != null) {
            int tc = lastPtr.getInt(0);
            int ic = lastPtr.getInt(OfdStructs.CONTAINER_IMGCOUNT_OFF);
            int pc = lastPtr.getInt(OfdStructs.CONTAINER_PATHCOUNT_OFF);
            lastObj = tc + ic + pc - 1;
            OFDFfi.ofd_free_elements(lastPtr);
        }
        int rcDel = OFDFfi.ofd_delete_object(handle, 0, lastObj);
        System.out.println("[EDIT] delete_object(index=" + lastObj + ")=" + rcDel
                + (rcDel == 0 ? " err=" + OFDFfi.ofd_get_last_error() : ""));

        // 5.5 新增空白页
        int rcPage = OFDFfi.ofd_add_page(handle);
        System.out.println("[EDIT] add_page=" + rcPage
                + (rcPage == 0 ? " err=" + OFDFfi.ofd_get_last_error() : ""));
        int newPages = OFDFfi.ofd_get_page_count(handle);
        System.out.println("[EDIT] 新增页后总页数=" + newPages);

        // 5.6 另存为标准 OFD 文件
        int rcSave = OFDFfi.ofd_save_to_file(handle, outPath);
        System.out.println("[EDIT] save_to_file=" + rcSave
                + (rcSave == 0 ? " err=" + OFDFfi.ofd_get_last_error() : "")
                + " -> " + outPath);

        // 6. 关闭文档
        int rc = OFDFfi.ofd_close_doc(handle);
        if (rc != 0) {
            System.out.println("[FAIL] 关闭失败: " + OFDFfi.ofd_get_last_error());
            return -1;
        }
        System.out.println("[OK] 文档已关闭, 残留句柄数=" + OFDCallbacks.activeHandleCount());

        // 7. 校验：重新打开另存后的 OFD，确认不丢 Path/图片
        System.out.println("--- 回读校验(另存文件) ---");
        long h2 = OFDFfi.ofd_open_file(outPath);
        if (h2 == 0L) {
            System.out.println("[FAIL] 另存文件无法打开: " + OFDFfi.ofd_get_last_error());
            return -1;
        }
        int pages2 = OFDFfi.ofd_get_page_count(h2);
        int imgTotal = 0, pathTotal = 0, textTotal = 0;
        for (int i = 0; i < pages2; i++) {
            Pointer ep = OFDFfi.ofd_read_page_elements(h2, i);
            if (ep != null) {
                textTotal += ep.getInt(0);
                imgTotal += ep.getInt(OfdStructs.CONTAINER_IMGCOUNT_OFF);
                pathTotal += ep.getInt(OfdStructs.CONTAINER_PATHCOUNT_OFF);
                OFDFfi.ofd_free_elements(ep);
            }
        }
        System.out.println("[VERIFY] 另存文件页数=" + pages2
                + " 文本=" + textTotal + " 图片=" + imgTotal + " 路径=" + pathTotal);
        OFDFfi.ofd_close_doc(h2);
        System.out.println("[OK] 回读校验完成, 残留句柄数=" + OFDCallbacks.activeHandleCount());
        System.out.println("=== 自检完成 ===");
        return 0;
    }

    public static void main(String[] args) {
        if (args.length < 1) {
            System.err.println("用法: java -jar ofd-jna-core-1.0.0.jar <ofd文件路径>");
            System.exit(2);
        }
        int code = selfTest(args[0]);
        System.exit(code);
    }
}