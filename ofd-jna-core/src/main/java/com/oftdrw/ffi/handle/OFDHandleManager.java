package com.oftdrw.ffi.handle;

import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.AtomicLong;

import org.ofdrw.reader.OFDReader;

/**
 * 文档句柄管理器。
 *
 * <p>将所有 OFDReader 原始 Java 对象封装为 long 句柄，对外只暴露句柄，不暴露原始对象。
 * 每个句柄分配 0 号保留（表示无效），保证并发安全，并配套 free 释放接口。</p>
 *
 * <p>所有 Java 句柄均需通过 {@link #free(long)} 释放，严防内存泄漏。</p>
 */
public final class OFDHandleManager {

    /** 无效句柄值，与 C 侧返回 0 约定一致。 */
    public static final long INVALID_HANDLE = 0L;

    /** 句柄自增分配器，0 保留给无效句柄，从 1 开始。 */
    private static final AtomicLong SEQUENCE = new AtomicLong(INVALID_HANDLE);

    /** 句柄 -> OFDReader 映射表。 */
    private static final Map<Long, OFDReader> HANDLE_TABLE = new ConcurrentHashMap<>();

    private OFDHandleManager() {
        // 工具类，禁止实例化
    }

    /**
     * 将 OFDReader 注册到句柄表，返回分配到的 long 句柄。
     *
     * @param reader OFDReader 文档对象，不可为空
     * @return 非零句柄；若文档为空返回 {@link #INVALID_HANDLE}
     */
    public static long register(OFDReader reader) {
        if (reader == null) {
            return INVALID_HANDLE;
        }
        long handle = SEQUENCE.incrementAndGet();
        HANDLE_TABLE.put(handle, reader);
        return handle;
    }

    /**
     * 根据句柄取出 OFDReader 对象。
     *
     * @param handle 文档句柄
     * @return OFDReader 对象；句柄无效或已被释放时返回 null
     */
    public static OFDReader get(long handle) {
        if (handle == INVALID_HANDLE) {
            return null;
        }
        return HANDLE_TABLE.get(handle);
    }

    /**
     * 释放句柄对应的 OFDReader 并关闭文档资源，然后从句柄表移除。
     *
     * @param handle 文档句柄
     * @return 释放成功返回 true；句柄无效或未注册返回 false
     */
    public static boolean free(long handle) {
        if (handle == INVALID_HANDLE) {
            return false;
        }
        OFDReader reader = HANDLE_TABLE.remove(handle);
        if (reader == null) {
            return false;
        }
        try {
            reader.close();
        } catch (Exception e) {
            // 关闭过程中的异常不影响句柄回收，交由上层错误缓冲处理
        }
        return true;
    }

    /** 当前存活的文档句柄数量（用于诊断/自检）。 */
    public static int activeCount() {
        return HANDLE_TABLE.size();
    }
}