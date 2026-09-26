package com.oftdrw.ffi.api;

import java.nio.charset.StandardCharsets;

/**
 * FFI 错误信息缓冲区管理器。
 *
 * <p>保存最后一次错误信息字符串，供 {@code ofd_get_last_error} 读取。
 * FFI 分配的错误字符串通过 {@link #toStringPtr()} 拷贝为字节数组后，
 * 由调用方使用 {@code ofd_free_string} 释放，严防内存泄漏。</p>
 */
public final class OFDErrorBuffer {

    private static volatile String lastError = "";

    private OFDErrorBuffer() {
        // 工具类，禁止实例化
    }

    /** 记录错误信息（线程安全写入）。 */
    public static void setError(String msg) {
        lastError = (msg == null) ? "" : msg;
    }

    /** 读取当前错误信息（线程安全读取）。 */
    public static String getError() {
        return lastError;
    }

    /**
     * 以 UTF-8 字节数组形式返回错误信息（带结尾 NUL）。
     * 该字节数组即 FFI 分配的字符串内存，由 ofd_free_string 释放。
     *
     * @return UTF-8 字节数组，含结尾 \0
     */
    public static byte[] toByteArrayWithNul() {
        byte[] data = lastError.getBytes(StandardCharsets.UTF_8);
        byte[] out = new byte[data.length + 1];
        System.arraycopy(data, 0, out, 0, data.length);
        out[data.length] = 0;
        return out;
    }
}