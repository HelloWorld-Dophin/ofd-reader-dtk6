/*
 * ofd_jni_wrapper.c — JNI C ABI 包装层（修正版）
 *
 * 导出 13 个纯 C 符号给 dlopen 使用，内部通过 JNI 调用
 * com.oftdrw.ffi.impl.OFDFfi 的静态方法。
 *
 * JVM 初始化：
 *   - 如果进程已嵌入 JVM → AttachCurrentThread
 *   - 否则 JNI_CreateJavaVM 创建新 JVM
 *   - classpath 通过 OFD_JNA_CLASSPATH 环境变量指定
 */

#include <jni.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <pthread.h>

/* ==================== JVM 全局状态 ==================== */

static JavaVM*  g_jvm     = NULL;
static JNIEnv*  g_env     = NULL;
static int      g_initialized = 0;
static pthread_mutex_t g_init_mutex = PTHREAD_MUTEX_INITIALIZER;

static jclass g_classOfdfi = NULL;   // com.oftdrw.ffi.impl.OFDFfi

/* 方法 ID */
static jmethodID mid_ofd_open_file       = NULL;
static jmethodID mid_ofd_close_doc       = NULL;
static jmethodID mid_ofd_get_page_count  = NULL;
static jmethodID mid_ofd_get_page_size   = NULL;  // void[](double[])
static jmethodID mid_ofd_get_last_error  = NULL;  // 返回 String
static jmethodID mid_ofd_read_page_elems = NULL;  // 返回 Pointer
static jmethodID mid_ofd_free_elements   = NULL;
static jmethodID mid_ofd_modify_text     = NULL;
static jmethodID mid_ofd_add_text        = NULL;
static jmethodID mid_ofd_add_rect_path   = NULL;
static jmethodID mid_ofd_delete_object   = NULL;
static jmethodID mid_ofd_add_page        = NULL;
static jmethodID mid_ofd_save_to_file    = NULL;

/* ==================== JVM 初始化 ==================== */

static int ensureJvm() {
    if (g_initialized) return 0;
    pthread_mutex_lock(&g_init_mutex);
    if (g_initialized) {
        pthread_mutex_unlock(&g_init_mutex);
        return 0;
    }

    JNIEnv* env = NULL;
    if (JNI_GetCreatedJavaVMs(&g_jvm, 1, NULL) == JNI_OK && g_jvm != NULL) {
        (*g_jvm)->AttachCurrentThread(g_jvm, (void**)&env, NULL);
        g_env = env;
    } else {
        JavaVMInitArgs vmArgs;
        vmArgs.version = JNI_VERSION_1_8;
        vmArgs.nOptions = 0;
        vmArgs.options = NULL;
        vmArgs.ignoreUnrecognized = JNI_FALSE;

        // classpath: OFD_JNA_CLASSPATH 环境变量（由启动方设置）
        const char* cp = getenv("OFD_JNA_CLASSPATH");
        if (cp == NULL || cp[0] == 0) {
            fprintf(stderr, "[ofd-jna-core] 警告: OFD_JNA_CLASSPATH 未设置\n");
            cp = ".";
        }
        char* opt_buf = malloc(strlen(cp) + 64);  // "-Djava.class.path=" + cp + '\0'
        if (opt_buf == NULL) {
            fprintf(stderr, "[ofd-jna-core] malloc 失败\n");
            pthread_mutex_unlock(&g_init_mutex);
            return -1;
        }
        snprintf(opt_buf, strlen(cp) + 64, "-Djava.class.path=%s", cp);
        JavaVMOption opts[2];
        opts[0].optionString = opt_buf;
        // 增加栈大小避免 JVM 创建时失败
        opts[1].optionString = "-Xmx512m";
        vmArgs.options = opts;
        vmArgs.nOptions = 2;

        jint ret = JNI_CreateJavaVM(&g_jvm, (void**)&g_env, &vmArgs);
        if (ret != JNI_OK) {
            fprintf(stderr, "[ofd-jna-core] JNI_CreateJavaVM 失败: %d\n", ret);
            pthread_mutex_unlock(&g_init_mutex);
            return -1;
        }
    }

    if (g_env == NULL) {
        fprintf(stderr, "[ofd-jna-core] JNIEnv 获取失败\n");
        pthread_mutex_unlock(&g_init_mutex);
        return -1;
    }

    // 找 OFDFfi 类
    jclass cls = (*g_env)->FindClass(g_env, "com/oftdrw/ffi/impl/OFDFfi");
    if (cls == NULL) {
        if ((*g_env)->ExceptionCheck(g_env)) {
            (*g_env)->ExceptionDescribe(g_env);
            (*g_env)->ExceptionClear(g_env);
        }
        fprintf(stderr, "[ofd-jna-core] 找不到 OFDFfi 类。请检查 OFD_JNA_CLASSPATH\n");
        pthread_mutex_unlock(&g_init_mutex);
        return -1;
    }
    g_classOfdfi = (*g_env)->NewGlobalRef(g_env, cls);

#define GET_MID(field, name, sig) do { \
    field = (*g_env)->GetStaticMethodID(g_env, g_classOfdfi, name, sig); \
    if (field == NULL) { \
        fprintf(stderr, "[ofd-jna-core] 方法 " name " " sig " 未找到\n"); \
        pthread_mutex_unlock(&g_init_mutex); \
        return -1; \
    } \
} while(0)

    GET_MID(mid_ofd_open_file,       "ofd_open_file",       "(Ljava/lang/String;)J");
    GET_MID(mid_ofd_close_doc,       "ofd_close_doc",       "(J)I");
    GET_MID(mid_ofd_get_page_count,  "ofd_get_page_count",  "(J)I");
    GET_MID(mid_ofd_get_page_size,   "ofd_get_page_size",   "(JI[D)I");
    GET_MID(mid_ofd_get_last_error,  "ofd_get_last_error",  "()Ljava/lang/String;");
    GET_MID(mid_ofd_read_page_elems, "ofd_read_page_elements", "(JI)Lcom/sun/jna/Pointer;");
    GET_MID(mid_ofd_free_elements,   "ofd_free_elements",   "(Lcom/sun/jna/Pointer;)V");
    GET_MID(mid_ofd_modify_text,     "ofd_modify_text",     "(JIILjava/lang/String;DI)I");
    GET_MID(mid_ofd_add_text,        "ofd_add_text",        "(JIDDLjava/lang/String;DI)I");
    GET_MID(mid_ofd_add_rect_path,   "ofd_add_rect_path",   "(JIDDDDDII)I");
    GET_MID(mid_ofd_delete_object,   "ofd_delete_object",   "(JII)I");
    GET_MID(mid_ofd_add_page,        "ofd_add_page",        "(J)I");
    GET_MID(mid_ofd_save_to_file,    "ofd_save_to_file",    "(JLjava/lang/String;)I");

#undef GET_MID

    g_initialized = 1;
    pthread_mutex_unlock(&g_init_mutex);
    return 0;
}

/* ==================== 工具宏 ==================== */

#define TO_JSTR(str) ((str) ? (*g_env)->NewStringUTF(g_env, (str)) : NULL)
#define JNI_CHECK() do { \
    if ((*g_env)->ExceptionCheck(g_env)) { \
        (*g_env)->ExceptionDescribe(g_env); \
        (*g_env)->ExceptionClear(g_env); \
    } \
} while(0)

/* 从 com.sun.jna.Pointer 取出 peer long 值 */
static void* pointerToVoid(jobject jptr) {
    if (jptr == NULL) return NULL;
    jclass pc = (*g_env)->FindClass(g_env, "com/sun/jna/Pointer");
    jfieldID fid = (*g_env)->GetFieldID(g_env, pc, "peer", "J");
    jlong peer = (*g_env)->GetLongField(g_env, jptr, fid);
    (*g_env)->DeleteLocalRef(g_env, pc);
    return (void*)(intptr_t)peer;
}

/* 构造 com.sun.jna.Pointer(long) */
static jobject voidToPointer(void* p) {
    if (p == NULL) return NULL;
    jclass pc = (*g_env)->FindClass(g_env, "com/sun/jna/Pointer");
    jmethodID ctor = (*g_env)->GetMethodID(g_env, pc, "<init>", "(J)V");
    jobject ptr = (*g_env)->NewObject(g_env, pc, ctor, (jlong)(intptr_t)p);
    (*g_env)->DeleteLocalRef(g_env, pc);
    return ptr;
}

/* ==================== 13 个 C ABI 导出函数 ==================== */

// ofd_open_file
long ofd_open_file(const char* path) {
    if (ensureJvm() != 0) return 0;
    jstring jpath = TO_JSTR(path);
    jlong result = (*g_env)->CallStaticLongMethod(g_env, g_classOfdfi, mid_ofd_open_file, jpath);
    JNI_CHECK();
    if (jpath) (*g_env)->DeleteLocalRef(g_env, jpath);
    return (long)result;
}

// ofd_close_doc
int ofd_close_doc(long handle) {
    if (ensureJvm() != 0) return -1;
    jint result = (*g_env)->CallStaticIntMethod(g_env, g_classOfdfi, mid_ofd_close_doc, (jlong)handle);
    JNI_CHECK();
    return (int)result;
}

// ofd_get_page_count
int ofd_get_page_count(long handle) {
    if (ensureJvm() != 0) return -1;
    jint result = (*g_env)->CallStaticIntMethod(g_env, g_classOfdfi, mid_ofd_get_page_count, (jlong)handle);
    JNI_CHECK();
    return (int)result;
}

// ofd_get_page_size — 输出 x, y, width, height (mm)
int ofd_get_page_size(long handle, int pageIndex, double* outX, double* outY,
                      double* outW, double* outH) {
    if (ensureJvm() != 0) return 0;
    if (!outX || !outY || !outW || !outH) return 0;
    jdoubleArray jArr = (*g_env)->NewDoubleArray(g_env, 4);
    if (!jArr) return 0;
    jint ok = (*g_env)->CallStaticIntMethod(g_env, g_classOfdfi, mid_ofd_get_page_size,
        (jlong)handle, (jint)pageIndex, jArr);
    JNI_CHECK();
    if (ok == 1) {
        jdouble vals[4];
        (*g_env)->GetDoubleArrayRegion(g_env, jArr, 0, 4, vals);
        *outX = vals[0]; *outY = vals[1]; *outW = vals[2]; *outH = vals[3];
    }
    (*g_env)->DeleteLocalRef(g_env, jArr);
    return (int)ok;
}

// ofd_get_last_error — 返回 UTF-8 字符串（用 Native.malloc 分配，caller 用 ofd_free_string 释放）
void* ofd_get_last_error(void) {
    if (ensureJvm() != 0) return NULL;
    jobject jstr = (*g_env)->CallStaticObjectMethod(g_env, g_classOfdfi, mid_ofd_get_last_error);
    JNI_CHECK();
    if (jstr == NULL) return NULL;

    // 转 UTF-8
    const char* utf = (*g_env)->GetStringUTFChars(g_env, (jstring)jstr, NULL);
    if (utf == NULL) {
        (*g_env)->DeleteLocalRef(g_env, jstr);
        return NULL;
    }
    size_t len = strlen(utf);
    // 用 Native.malloc（和 OFDCallbacks 分配策略一致）
    void* mem = NULL;
    // 通过 JNI 调 Native.malloc
    jclass nativeCls = (*g_env)->FindClass(g_env, "com/sun/jna/Native");
    jmethodID mallocMid = (*g_env)->GetStaticMethodID(g_env, nativeCls, "malloc", "(J)J");
    jlong addr = (*g_env)->CallStaticLongMethod(g_env, nativeCls, mallocMid, (jlong)(len + 1));
    mem = (void*)(intptr_t)addr;
    (*g_env)->DeleteLocalRef(g_env, nativeCls);

    if (mem) {
        memcpy(mem, utf, len + 1);
    }
    (*g_env)->ReleaseStringUTFChars(g_env, (jstring)jstr, utf);
    (*g_env)->DeleteLocalRef(g_env, jstr);
    return mem;
}

// ofd_free_string — 释放 ofd_get_last_error 返回的内存
void ofd_free_string(void* p) {
    if (ensureJvm() != 0 || p == NULL) return;
    jclass nativeCls = (*g_env)->FindClass(g_env, "com/sun/jna/Native");
    jmethodID freeMid = (*g_env)->GetStaticMethodID(g_env, nativeCls, "free", "(J)V");
    (*g_env)->CallStaticVoidMethod(g_env, nativeCls, freeMid, (jlong)(intptr_t)p);
    JNI_CHECK();
    (*g_env)->DeleteLocalRef(g_env, nativeCls);
}

// ofd_read_page_elements
void* ofd_read_page_elements(long handle, int pageIndex) {
    if (ensureJvm() != 0) return NULL;
    jobject jptr = (*g_env)->CallStaticObjectMethod(g_env, g_classOfdfi, mid_ofd_read_page_elems, (jlong)handle, (jint)pageIndex);
    JNI_CHECK();
    if (jptr == NULL) return NULL;
    void* result = pointerToVoid(jptr);
    (*g_env)->DeleteLocalRef(g_env, jptr);
    return result;
}

// ofd_free_elements
void ofd_free_elements(void* elements) {
    if (ensureJvm() != 0 || elements == NULL) return;
    jobject jptr = voidToPointer(elements);
    if (jptr) {
        (*g_env)->CallStaticVoidMethod(g_env, g_classOfdfi, mid_ofd_free_elements, jptr);
        JNI_CHECK();
        (*g_env)->DeleteLocalRef(g_env, jptr);
    }
}

// ofd_modify_text
int ofd_modify_text(long handle, int pageIdx, int textItemIndex,
                    const char* newText, double fontSize, int colorARGB) {
    if (ensureJvm() != 0) return 0;
    jstring jtext = TO_JSTR(newText);
    jint result = (*g_env)->CallStaticIntMethod(g_env, g_classOfdfi, mid_ofd_modify_text,
        (jlong)handle, (jint)pageIdx, (jint)textItemIndex, jtext, (jdouble)fontSize, (jint)colorARGB);
    JNI_CHECK();
    if (jtext) (*g_env)->DeleteLocalRef(g_env, jtext);
    return (int)result;
}

// ofd_add_text
int ofd_add_text(long handle, int pageIdx, double x, double y,
                 const char* text, double fontSize, int colorARGB) {
    if (ensureJvm() != 0) return 0;
    jstring jtext = TO_JSTR(text);
    jint result = (*g_env)->CallStaticIntMethod(g_env, g_classOfdfi, mid_ofd_add_text,
        (jlong)handle, (jint)pageIdx, (jdouble)x, (jdouble)y, jtext, (jdouble)fontSize, (jint)colorARGB);
    JNI_CHECK();
    if (jtext) (*g_env)->DeleteLocalRef(g_env, jtext);
    return (int)result;
}

// ofd_add_rect_path
int ofd_add_rect_path(long handle, int pageIdx,
                      double x, double y, double w, double h,
                      double strokeWidth, int strokeColor, int fillColor) {
    if (ensureJvm() != 0) return 0;
    jint result = (*g_env)->CallStaticIntMethod(g_env, g_classOfdfi, mid_ofd_add_rect_path,
        (jlong)handle, (jint)pageIdx, (jdouble)x, (jdouble)y, (jdouble)w, (jdouble)h,
        (jdouble)strokeWidth, (jint)strokeColor, (jint)fillColor);
    JNI_CHECK();
    return (int)result;
}

// ofd_delete_object
int ofd_delete_object(long handle, int pageIdx, int objIndex) {
    if (ensureJvm() != 0) return 0;
    jint result = (*g_env)->CallStaticIntMethod(g_env, g_classOfdfi, mid_ofd_delete_object,
        (jlong)handle, (jint)pageIdx, (jint)objIndex);
    JNI_CHECK();
    return (int)result;
}

// ofd_add_page
int ofd_add_page(long handle) {
    if (ensureJvm() != 0) return 0;
    jint result = (*g_env)->CallStaticIntMethod(g_env, g_classOfdfi, mid_ofd_add_page, (jlong)handle);
    JNI_CHECK();
    return (int)result;
}

// ofd_save_to_file
int ofd_save_to_file(long handle, const char* outputPath) {
    if (ensureJvm() != 0) return 0;
    jstring jpath = TO_JSTR(outputPath);
    jint result = (*g_env)->CallStaticIntMethod(g_env, g_classOfdfi, mid_ofd_save_to_file, (jlong)handle, jpath);
    JNI_CHECK();
    if (jpath) (*g_env)->DeleteLocalRef(g_env, jpath);
    return (int)result;
}
