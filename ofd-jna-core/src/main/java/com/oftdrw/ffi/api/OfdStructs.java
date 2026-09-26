package com.oftdrw.ffi.api;

import com.sun.jna.Pointer;
import com.sun.jna.Structure;

import java.util.Arrays;
import java.util.List;

/**
 * OFD 页面元素 C 映射结构体。
 *
 * <p>定义供 FFI 与 C 侧交换的二进制结构：文本、图片、路径三种元素，
 * 以及承载全部元素的容器结构 {@link OfdPageElements}。</p>
 *
 * <p>内存约定：结构体内字符串/字节数组为 FFI 分配内存，
 * 由容器释放接口 {@code ofd_free_elements} 一并释放，严防内存泄漏。</p>
 */
public final class OfdStructs {

    private OfdStructs() {
        // 工具类，禁止实例化
    }

    /** CTM 变换矩阵元素个数（OFD 为 6 个浮点：a b c d e f）。 */
    public static final int CTM_SIZE = 6;

    /** 各结构体的原生内存字节大小（用于 FFI 数组分配与释放）。 */
    public static final int TEXT_SIZE = new OfdTextItem().size();
    public static final int IMAGE_SIZE = new OfdImageItem().size();
    public static final int PATH_SIZE = new OfdPathItem().size();
    public static final int CONTAINER_SIZE = new OfdPageElements().size();

    /* ==================== 字段偏移常量（64 位布局，供 C 侧按偏移读写） ==================== */

    // OfdPageElements
    public static final int CONTAINER_TEXTCOUNT_OFF  = 0;
    public static final int CONTAINER_TEXTS_OFF      = 8;
    public static final int CONTAINER_IMGCOUNT_OFF   = 16;
    public static final int CONTAINER_IMAGES_OFF     = 24;
    public static final int CONTAINER_PATHCOUNT_OFF  = 32;
    public static final int CONTAINER_PATHS_OFF      = 40;

    // OfdTextItem
    public static final int TEXT_CONTENT_OFF    = 0;
    public static final int TEXT_FONTSIZE_OFF   = 8;
    public static final int TEXT_ARGB_OFF       = 16;
    public static final int TEXT_CTM_OFF        = 24;
    public static final int TEXT_X_OFF          = 72;
    public static final int TEXT_Y_OFF          = 80;
    public static final int TEXT_FONTID_OFF     = 88;
    public static final int TEXT_BW_OFF         = 96;
    public static final int TEXT_BH_OFF         = 104;
    public static final int TEXT_ADV_OFF        = 112;  // contentAdvance (double 8B)
    public static final int TEXT_FONTNAME_OFF   = 120;
    public static final int TEXT_GRADIENT_OFF   = 128;

    // OfdImageItem
    public static final int IMAGE_RESID_OFF    = 0;
    public static final int IMAGE_WIDTH_OFF    = 8;
    public static final int IMAGE_HEIGHT_OFF   = 16;
    public static final int IMAGE_CTM_OFF      = 24;
    public static final int IMAGE_DATA_OFF     = 72;
    public static final int IMAGE_DATASIZE_OFF = 80;

    // OfdPathItem (pack=8, 总大小 104B，与 JNA Structure.size() 实测一致)
    public static final int PATH_DATA_OFF        = 0;
    public static final int PATH_STROKE_OFF      = 8;   // int, 4B
    public static final int PATH_FILL_OFF        = 12;  // int, 4B
    public static final int PATH_WIDTH_OFF       = 16;  // double, 8B
    public static final int PATH_CTM_OFF         = 24;  // ctm[6] 48B → 结束 72
    // 下面是新增字段，pack=8 对齐
    public static final int PATH_STROKEFLAG_OFF  = 72;  // int, 4B
    public static final int PATH_FILLFLAG_OFF    = 76;  // int, 4B
    public static final int PATH_MITERLIMIT_OFF  = 80;  // double, 8B
    public static final int PATH_CAP_OFF         = 88;  // int, 4B
    public static final int PATH_JOIN_OFF        = 92;  // int, 4B
    public static final int PATH_ALPHA_OFF       = 96;  // int, 4B
    public static final int PATH_GRADIENT_OFF    = 104; // Pointer, 8B → sizeof=112
    // strokeFlag/fillFlag: 0=未设置 1=true 2=false
    // capType/joinType: 0=OFD默认值 1=Round 2=Bevel/Square

    /**
     * 文本对象结构体。
     */
    public static class OfdTextItem extends Structure {
        /** 文字内容（UTF-8，FFI 分配，随容器释放）。 */
        public Pointer content;
        /** 字号（单位：毫米，OFD 默认坐标系）。 */
        public double fontSize;
        /** ARGB 颜色（0xAARRGGBB）。 */
        public int argb;
        /** CTM 变换矩阵 6 个浮点数。 */
        public double[] ctm = new double[CTM_SIZE];
        /** 文本坐标 X。 */
        public double x;
        /** 文本坐标 Y。 */
        public double y;
        /** 字体资源 ID（int32，原 long 拆分为 ID + 名字指针）。 */
        public int fontId;
        /** TextObject boundary 宽度（毫米），C++ 侧自动换行用。 */
        public double boundaryWidth;
        /** TextObject boundary 高度（毫米），C++ 侧裁剪用。 */
        public double boundaryHeight;
        /** DeltaX 累计字符间距（毫米），C++ 用它缩放过宽字体 + 居中。0 表示无 DeltaX。 */
        public double contentAdvance;
        /** 字体名 UTF-8 字符串指针（JNI 分配，随容器释放），NULL 表示无字体名。 */
        public Pointer fontNamePtr;
        /** 渐变填充 FFI 指针（OfdGradient*），NULL 表示纯色填充。 */
        public Pointer gradient;

        public OfdTextItem() {
            super();
        }

        public OfdTextItem(Pointer p) {
            super(p);
            read();
        }

        @Override
        protected List<String> getFieldOrder() {
            return Arrays.asList("content", "fontSize", "argb", "ctm", "x", "y", "fontId", "boundaryWidth", "boundaryHeight", "contentAdvance", "fontNamePtr", "gradient");
        }
    }

    /**
     * 图片对象结构体。
     */
    public static class OfdImageItem extends Structure {
        /** 图片资源 ID。 */
        public long resourceId;
        /** 显示宽度。 */
        public double width;
        /** 显示高度。 */
        public double height;
        /** CTM 变换矩阵 6 个浮点数。 */
        public double[] ctm = new double[CTM_SIZE];
        /** 图片原始二进制字节（FFI 分配，随容器释放）。 */
        public Pointer data;
        /** 图片字节长度。 */
        public int dataSize;

        public OfdImageItem() {
            super();
        }

        public OfdImageItem(Pointer p) {
            super(p);
            read();
        }

        @Override
        protected List<String> getFieldOrder() {
            return Arrays.asList("resourceId", "width", "height", "ctm", "data", "dataSize");
        }
    }

    /**
     * 路径对象结构体。
     */
    public static class OfdPathItem extends Structure {
        /** 路径指令序列（M/L/C 等，UTF-8，FFI 分配，随容器释放）。 */
        public Pointer data;
        /** 描边颜色 ARGB（0xAARRGGBB），未定义时值无意义。 */
        public int strokeArgb;
        /** 填充颜色 ARGB（0xAARRGGBB），未定义时值无意义。 */
        public int fillArgb;
        /** 描边宽度（单位：毫米）。 */
        public double strokeWidth;
        /** CTM 变换矩阵 6 个浮点数。 */
        public double[] ctm = new double[CTM_SIZE];

        /** Stroke 属性：0=未设置(由 strokeArgb alpha 判断) 1=true(描边) 2=false(不描边) */
        public int strokeFlag;
        /** Fill 属性：0=未设置(由 fillArgb alpha 判断) 1=true(填充) 2=false(不填充) */
        public int fillFlag;
        /** 尖角限制系数（OFD 默认 10），&lt;=0 表示未设置。 */
        public double miterLimit;
        /** 端点样式：0=ButtCap(OFD 默认) 1=RoundCap 2=SquareCap */
        public int capType;
        /** 连接样式：0=MiterJoin(OFD 默认) 1=RoundJoin 2=BevelJoin */
        public int joinType;
        /** 全局透明度 0-255，&lt;=0 表示未设置（使用 stroke/fill 颜色自身 alpha）。 */
        public int alpha;
        /** 渐变填充 FFI 指针（OfdGradient*），NULL 表示纯色填充。 */
        public Pointer gradient;

        public OfdPathItem() {
            super();
        }

        public OfdPathItem(Pointer p) {
            super(p);
            read();
        }

        @Override
        protected List<String> getFieldOrder() {
            return Arrays.asList("data", "strokeArgb", "fillArgb", "strokeWidth", "ctm",
                "strokeFlag", "fillFlag", "miterLimit", "capType", "joinType", "alpha", "gradient");
        }
    }

    /**
     * 页面元素容器结构体：承载一页全部文本、图片、路径对象。
     */
    public static class OfdPageElements extends Structure {
        /** 文本元素数量。 */
        public int textCount;
        /** 文本元素数组指针（OfdTextItem*）。 */
        public Pointer texts;
        /** 图片元素数量。 */
        public int imageCount;
        /** 图片元素数组指针（OfdImageItem*）。 */
        public Pointer images;
        /** 路径元素数量。 */
        public int pathCount;
        /** 路径元素数组指针（OfdPathItem*）。 */
        public Pointer paths;

        public OfdPageElements() {
            super();
        }

        public OfdPageElements(Pointer p) {
            super(p);
            read();
        }

        @Override
        protected List<String> getFieldOrder() {
            return Arrays.asList("textCount", "texts", "imageCount", "images", "pathCount", "paths");
        }
    }
}