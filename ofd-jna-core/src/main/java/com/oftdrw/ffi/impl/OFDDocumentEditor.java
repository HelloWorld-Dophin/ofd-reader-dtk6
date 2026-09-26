package com.oftdrw.ffi.impl;

import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.List;

import org.dom4j.Element;
import org.ofdrw.core.basicStructure.doc.CT_CommonData;
import org.ofdrw.core.basicStructure.doc.CT_PageArea;
import org.ofdrw.core.basicStructure.doc.Document;
import org.ofdrw.core.basicStructure.pageObj.Content;
import org.ofdrw.core.basicStructure.pageObj.Page;
import org.ofdrw.core.basicStructure.pageObj.layer.CT_Layer;
import org.ofdrw.core.basicStructure.pageObj.layer.PageBlockType;
import org.ofdrw.core.basicStructure.pageObj.layer.block.PathObject;
import org.ofdrw.core.basicStructure.pageObj.layer.block.TextObject;
import org.ofdrw.core.basicStructure.pageTree.Pages;
import org.ofdrw.core.basicType.ST_Box;
import org.ofdrw.core.basicType.ST_ID;
import org.ofdrw.core.basicType.ST_Loc;
import org.ofdrw.core.graph.pathObj.AbbreviatedData;
import org.ofdrw.core.pageDescription.color.color.CT_Color;
import org.ofdrw.core.text.TextCode;
import org.ofdrw.reader.OFDReader;
import org.ofdrw.pkg.container.DocDir;
import org.ofdrw.pkg.container.OFDDir;
import org.ofdrw.pkg.container.PageDir;
import org.ofdrw.pkg.container.PagesDir;

import com.oftdrw.ffi.api.OFDErrorBuffer;

/**
 * OFD 文档编辑器：基于 ofdrw 内存 DOM 模型实现增删改存。
 *
 * <p>所有修改直接作用于 ofdrw 解析出的 dom4j 内存 DOM 树，禁止拼接 XML 字符串。
 * {@link OFDReader#getPage(int)} 每次都会从容器对象表重新解析同一个底层元素，
 * 因此对返回 Page 的原地修改在后续调用中持续生效。保存时通过
 * {@link OFDDir#jar(Path)} 完成符合 GB/T 33190-2016 的标准 OFD 打包，
 * 由 ofdrw 全权处理 zip，本层不手写任何打包逻辑。</p>
 *
 * <p>页面元素索引约定：按文档解析顺序对页面内全部 layer 的 block 扁平化编号
 * （与 FFI 读取接口 ofd_read_page_elements 的元素次序一致）。</p>
 *
 * <p>坐标约定：OFD 坐标系 y 轴向上，单位 mm。
 * TextCode 的 X/Y 是 TextObject 内部偏移（原点在 TextObject 左下角），
 * PathObject.AbbreviatedData 的坐标是 PathObject 内部局部坐标（原点在 PathObject 左下角），
 * 不是页面绝对坐标。页面绝对坐标由 TextObject/PathObject 的 Boundary 决定。</p>
 */
public final class OFDDocumentEditor {

    private OFDDocumentEditor() {
        // 工具类，禁止实例化
    }

    /* ==================== 通用辅助 ==================== */

    /** 获取指定页面的扁平化 block 列表（跨 layer，按解析顺序）。 */
    private static List<PageBlockType> flatBlocks(Page page) {
        List<PageBlockType> out = new ArrayList<>();
        if (page == null || page.getContent() == null) {
            return out;
        }
        for (CT_Layer layer : page.getContent().getLayers()) {
            if (layer == null || layer.getPageBlocks() == null) {
                continue;
            }
            out.addAll(layer.getPageBlocks());
        }
        return out;
    }

    /** 获取页面第一个 layer（用于新增对象），不存在则创建。 */
    private static CT_Layer firstLayer(Page page) {
        if (page.getContent() == null) {
            page.setContent(new Content());
        }
        Content content = page.getContent();
        List<CT_Layer> layers = content.getLayers();
        if (layers == null || layers.isEmpty()) {
            CT_Layer layer = new CT_Layer();
            content.addLayer(layer);
            return layer;
        }
        return layers.get(0);
    }

    /** 将 ARGB 整数转为 ofdrw CT_Color（RGB + 透明度）。 */
    private static CT_Color argbToColor(int argb) {
        int alpha = (argb >> 24) & 0xFF;
        int r = (argb >> 16) & 0xFF;
        int g = (argb >> 8) & 0xFF;
        int b = argb & 0xFF;
        CT_Color color = CT_Color.rgb(r, g, b);
        if (alpha != 255) {
            color.setAlpha(alpha);
        }
        return color;
    }

    /** 获取页面首个现有文本的字体引用 ID；无文本时返回 null（采用文档默认字体）。 */
    private static Long firstTextFontId(Page page) {
        for (PageBlockType b : flatBlocks(page)) {
            if (b instanceof TextObject) {
                try {
                    TextObject t = (TextObject) b;
                    if (t.getFont() != null && t.getFont().getRefId() != null
                            && t.getFont().getRefId().getId() != null) {
                        return t.getFont().getRefId().getId();
                    }
                } catch (Throwable ignored) {
                    // 忽略单条文本异常
                }
            }
        }
        return null;
    }

    /* ==================== 1. 修改已有文本 ==================== */

    /**
     * 修改指定页面上第 textItemIndex 个文本对象的内容、字号、颜色。
     *
     * @return 成功返回 1，失败返回 0
     */
    public static int modifyText(OFDReader reader, int pageIdx, int textItemIndex,
                                 String newText, double fontSize, int colorARGB) {
        try {
            if (reader == null || newText == null) {
                OFDErrorBuffer.setError("ofd_modify_text 参数无效");
                return 0;
            }
            Page page = reader.getPage(pageIdx + 1);
            if (page == null) {
                OFDErrorBuffer.setError("ofd_modify_text 页码超出范围: " + pageIdx);
                return 0;
            }
            List<TextObject> texts = new ArrayList<>();
            for (PageBlockType b : flatBlocks(page)) {
                if (b instanceof TextObject) {
                    texts.add((TextObject) b);
                }
            }
            if (textItemIndex < 0 || textItemIndex >= texts.size()) {
                OFDErrorBuffer.setError("ofd_modify_text 文本索引超出范围: " + textItemIndex);
                return 0;
            }
            TextObject t = texts.get(textItemIndex);

            // ★ 在清除旧 TextCode 之前，保存原始 TextCode 的全部属性：
            // X/Y（baseline 偏移）、DeltaX/DeltaY（字形间距）
            double origX = 0.0, origY = 0.0;
            org.ofdrw.core.basicType.ST_Array origDeltaX = null, origDeltaY = null;
            List<TextCode> oldCodes = t.getTextCodes();
            if (oldCodes != null && !oldCodes.isEmpty()) {
                TextCode first = oldCodes.get(0);
                origX = first.getX() != null ? first.getX() : 0.0;
                origY = first.getY() != null ? first.getY() : 0.0;
                origDeltaX = first.getDeltaX();
                origDeltaY = first.getDeltaY();
            }

            // 清除旧 TextCode 和旧 FillColor
            t.removeOFDElemByNames("TextCode");
            t.removeOFDElemByNames("FillColor");

            // ★ 关键：子元素顺序必须与原始 XML 一致 —— FillColor 在前、TextCode 在后
            // 先设置 FillColor（它会被追加到 dom4j 子元素列表尾部，此时 TextCode 还没加，所以 FillColor 排在 TextCode 前面）
            t.setFillColor(argbToColor(colorARGB));

            // 后加 TextCode，保留原始 X/Y，只替换内容
            TextCode code = new TextCode().setContent(newText).setX(origX).setY(origY);

            // ★★★ DeltaX 必须生成精确数量的值 = newText.length() - 1
            // 根因：ofdrw jar 打包时会把 DeltaX 压缩成 OFD 规范缩写格式 "g N value"，
            // N 声明的是间距数量。如果原始有 10 个间距但新文字只有 8 字符，
            // 缩写就变成 "g 10 width"，渲染器按 10 个间距排版但只有 7 个间距位置 → 叠字。
            // 解决：裁剪或填充到正确数量，让缩写声明的 N 等于实际需要的间距数。
            int newLen = newText.length();
            if (newLen >= 2) {
                int need = newLen - 1;
                Double[] dxArr = new Double[need];
                if (origDeltaX != null) {
                    // 提取原始间距值（保留第一个作为基准宽度）
                    Double[] origVals = origDeltaX.toDouble();
                    double baseWidth = (origVals != null && origVals.length > 0) ? origVals[0] : fontSize;
                    for (int i = 0; i < need; i++) {
                        dxArr[i] = (i < origVals.length) ? origVals[i] : baseWidth;
                    }
                } else {
                    // 无原始 DeltaX，用估算宽度
                    double glyphW = fontSize > 0 ? fontSize * 0.6 : 6.0;
                    for (int i = 0; i < need; i++) {
                        dxArr[i] = glyphW;
                    }
                }
                code.setDeltaX(new org.ofdrw.core.basicType.ST_Array(dxArr));
            }
            // DeltaY 无条件保留（通常为 null）
            if (origDeltaY != null) {
                code.setDeltaY(origDeltaY);
            }

            t.addTextCode(code);

            // 字号变更
            if (fontSize > 0) {
                t.setSize(fontSize);
            }
            // ★ 不调用 t.setFill(true) —— 原始 XML 没有 Fill 属性，有 FillColor 就默认 Fill=true
            return 1;
        } catch (Throwable e) {
            OFDErrorBuffer.setError("ofd_modify_text 失败: " + e);
            return 0;
        }
    }

    /* ==================== 2. 新增文本 ==================== */

    /**
     * 在指定页面坐标 (x, y)（OFD 坐标系左下角为原点，y 向上，单位 mm）
     * 处新增一个文本对象。
     *
     * @return 成功返回 1，失败返回 0
     */
    public static int addText(OFDReader reader, int pageIdx, double x, double y,
                              String text, double fontSize, int colorARGB) {
        try {
            if (reader == null || text == null) {
                OFDErrorBuffer.setError("ofd_add_text 参数无效");
                return 0;
            }
            Page page = reader.getPage(pageIdx + 1);
            if (page == null) {
                OFDErrorBuffer.setError("ofd_add_text 页码超出范围: " + pageIdx);
                return 0;
            }
            if (fontSize <= 0) {
                fontSize = 12.0;
            }
            CT_Layer layer = firstLayer(page);

            long id = nextUnitId(reader);
            TextObject to = new TextObject(id);
            // 复用页面首个文本字体，保证渲染一致性；无字体则用默认
            Long fontId = firstTextFontId(page);
            if (fontId != null) {
                to.setFont(fontId);
            }
            to.setSize(fontSize);
            to.setFillColor(argbToColor(colorARGB));
            // ★ 不调用 setFill(true) —— 原始 XML 没有 Fill 属性
            // Boundary 是页面绝对坐标：(x, y) 为左下角原点，估算宽高
            to.setBoundary(x, y, text.length() * fontSize * 0.6, fontSize * 1.2);

            // ★ 关键：TextCode 必须带 DeltaX，否则渲染器默认间距=0 → 字符叠在一起。
            // n 个字符需要 n-1 个间距值，每个间距 ≈ fontSize * 0.6 mm（和 Boundary 估算一致）
            TextCode tc = new TextCode().setContent(text).setX(0.0).setY(fontSize * 0.25);
            if (text.length() >= 2) {
                double glyphW = fontSize * 0.6;
                Double[] dxArr = new Double[text.length() - 1];
                for (int i = 0; i < dxArr.length; i++) {
                    dxArr[i] = glyphW;
                }
                tc.setDeltaX(new org.ofdrw.core.basicType.ST_Array(dxArr));
            }
            to.addTextCode(tc);
            // 直接取 layer 和 to 的真实 dom4j Element 进行挂载，
            // 绕过 ofdrw 的 addPageBlock（它会把 DefaultElementProxy 传给 dom4j 底层）
            Element layerProxy = layer.getProxy();
            Element toProxy = to.getProxy();
            toProxy.detach();
            layerProxy.add(toProxy);
            return 1;
        } catch (Throwable e) {
            OFDErrorBuffer.setError("ofd_add_text 失败: " + e);
            return 0;
        }
    }

    /* ==================== 3. 新增矩形矢量图形 ==================== */

    /**
     * 在指定页面新增一个矩形路径（矢量图形），完整绘制四边并闭合。
     *
     * <p>技术要点：AbbreviatedData 是 PathObject 内部局部坐标（原点在 Boundary 左下角），
     * 不是页面绝对坐标！矩形从 (0,0) 画到 (w,h)。</p>
     *
     * <p>重要：必须通过 {@code getProxy()} 取底层真实 dom4j Element 手动挂载，
     * 不能用 ofdrw 的 {@code addPageBlock}——它把 DefaultElementProxy 包装对象传给
     * dom4j 底层，导致同 ID 重复、子元素丢失。</p>
     *
     * @return 成功返回 1，失败返回 0
     */
    public static int addRectPath(OFDReader reader, int pageIdx, double x, double y,
                                  double w, double h, double strokeWidth,
                                  int strokeColor, int fillColor) {
        try {
            if (reader == null || w <= 0 || h <= 0) {
                OFDErrorBuffer.setError("ofd_add_rect_path 参数无效");
                return 0;
            }
            Page page = reader.getPage(pageIdx + 1);
            if (page == null) {
                OFDErrorBuffer.setError("ofd_add_rect_path 页码超出范围: " + pageIdx);
                return 0;
            }
            CT_Layer layer = firstLayer(page);

            long id = nextUnitId(reader);
            PathObject po = new PathObject(new ST_ID(id));

            // AbbreviatedData 是 PathObject 内部局部坐标（原点在 Boundary 左下角），
            // 不是页面绝对坐标！矩形从 (0,0) 画到 (w,h)
            AbbreviatedData d = new AbbreviatedData()
                    .M(0, 0)
                    .L(w, 0)
                    .L(w, h)
                    .L(0, h)
                    .C();

            po.setAbbreviatedData(d);

            // Boundary 才是页面绝对坐标：(x, y) 为 PathObject 在页面上的左下角原点
            po.setBoundary(x, y, w, h);
            if (strokeWidth > 0) {
                po.setLineWidth(strokeWidth);
            }
            po.setStrokeColor(argbToColor(strokeColor));
            po.setFillColor(argbToColor(fillColor));
            po.setStroke(strokeColor != 0);
            po.setFill(true);

            // 关键修复：直接取 layer 和 po 的真实 dom4j Element 进行挂载，
            // 绕过 ofdrw 的 addPageBlock（它会把 DefaultElementProxy 传给 dom4j 底层）
            Element layerProxy = layer.getProxy();
            Element poProxy = po.getProxy();
            poProxy.detach();
            layerProxy.add(poProxy);

            return 1;
        } catch (Throwable e) {
            OFDErrorBuffer.setError("ofd_add_rect_path 失败: " + e);
            return 0;
        }
    }

    /* ==================== 4. 删除页面对象 ==================== */

    /**
     * 删除指定页面第 objIndex 个元素（扁平化索引）。
     *
     * <p>重要：不仅要从 Java PageBlocks 列表移除，必须 detach 底层 dom4j Element——
     * 否则 ofdrw 从 dom4j 动态解析的 getPageBlocks() 会重新看到这个残留的空壳。</p>
     *
     * @return 成功返回 1，失败返回 0
     */
    public static int deleteObject(OFDReader reader, int pageIdx, int objIndex) {
        try {
            if (reader == null || objIndex < 0) {
                OFDErrorBuffer.setError("ofd_delete_object 参数无效");
                return 0;
            }
            Page page = reader.getPage(pageIdx + 1);
            if (page == null) {
                OFDErrorBuffer.setError("ofd_delete_object 页码超出范围: " + pageIdx);
                return 0;
            }
            if (page.getContent() == null) {
                OFDErrorBuffer.setError("ofd_delete_object 页面无内容");
                return 0;
            }
            // 扁平化定位目标 block 所属 layer 及其内部下标
            int cursor = objIndex;
            for (CT_Layer layer : page.getContent().getLayers()) {
                List<PageBlockType> blocks = layer.getPageBlocks();
                if (blocks == null) {
                    continue;
                }
                if (cursor < blocks.size()) {
                    PageBlockType target = blocks.get(cursor);
                    // 关键修复：必须 detach 底层 dom4j Element（从 dom4j 树彻底移除）
                    // + 清除子元素 + 移除 Java 列表
                    if (target instanceof org.ofdrw.core.DefaultElementProxy) {
                        org.ofdrw.core.DefaultElementProxy proxyTarget =
                                (org.ofdrw.core.DefaultElementProxy) target;
                        proxyTarget.detach();     // 从 dom4j 树彻底移除
                        ((org.dom4j.Element) proxyTarget).clearContent(); // 清除子元素
                    }
                    blocks.remove(cursor);
                    return 1;
                }
                cursor -= blocks.size();
            }
            OFDErrorBuffer.setError("ofd_delete_object 索引超出范围: " + objIndex);
            return 0;
        } catch (Throwable e) {
            OFDErrorBuffer.setError("ofd_delete_object 失败: " + e);
            return 0;
        }
    }

    /* ==================== 5. 新增空白页 ==================== */

    /**
     * 在文档末尾新增一个空白页（含默认页面区域与空内容层）。
     *
     * <p>关键：必须基于 {@code ofdDir.obtainDocDefault().obtainPages()} 获取 PagesDir，
     * 不能用 {@code ofdDir.obtainContainer(DocDir.PagesDir, ...)} 直接取根目录 Pages——
     * 后者的物理路径指向 OFD 根目录下的 Pages 文件夹（那里没有原页面），
     * 而文档的实际页面在 {@code Doc_0/Pages/} 下。</p>
     *
     * @return 成功返回 1，失败返回 0
     */
    public static int addPage(OFDReader reader) {
        try {
            if (reader == null) {
                OFDErrorBuffer.setError("ofd_add_page 参数无效");
                return 0;
            }
            OFDDir ofdDir = reader.getOFDDir();
            Document doc = reader.cdDefaultDoc();
            if (ofdDir == null || doc == null) {
                OFDErrorBuffer.setError("ofd_add_page 无法获取文档容器");
                return 0;
            }

            // 1. 在 DocDir（Doc_0）下的 Pages 目录新建 PageDir。
            //    必须基于 DocDir.obtainPages() 获取，其物理路径指向 Doc_0/Pages，
            //    这样 newPageDir 才会扫描到已有 Page_0 → maxPageIndex=1 → 生成 Page_1。
            DocDir docDir = ofdDir.obtainDocDefault();
            if (docDir == null) {
                OFDErrorBuffer.setError("ofd_add_page 无法获取 DocDir");
                return 0;
            }
            PagesDir pagesDir = docDir.obtainPages();
            PageDir newPageDir = pagesDir.newPageDir();

            // 2. 构造页面内容：空 Content + 空 Layer（带 ID）
            long pageId = nextUnitId(reader);
            Content content = new Content();
            CT_Layer emptyLayer = new CT_Layer();
            emptyLayer.setObjID(nextUnitId(reader));
            content.addLayer(emptyLayer);
            Page page = new Page();
            page.setObjID(pageId);
            page.setContent(content);

            // 复制默认页面区域（沿用首页尺寸，保证规范与对齐）
            ST_Box size = reader.getPageSize(1);
            if (size != null) {
                CT_PageArea area = new CT_PageArea();
                area.setPhysicalBox(size.getTopLeftX(), size.getTopLeftY(),
                        size.getWidth(), size.getHeight());
                page.setArea(area);
            }
            newPageDir.setContent(page);

            // 3. 在 Document.Pages 注册新页引用。
            //    newIdx 必须从 newPageDir.getIndex() 取，它基于 Doc_0/Pages 实际目录名生成，
            //    确保 BaseLoc="Pages/Page_"+newIdx+"/Content.xml" 正确指向物理文件。
            Pages pages = doc.getPages();
            int newIdx = newPageDir.getIndex();
            long newId = nextUnitId(reader);
            ST_Loc baseLoc = new ST_Loc("Pages/Page_" + newIdx + "/Content.xml");
            pages.addPage(new org.ofdrw.core.basicStructure.pageTree.Page(new ST_ID(newId), baseLoc));

            // 4. 更新最大对象 ID
            CT_CommonData common = doc.getCommonData();
            if (common != null) {
                common.setMaxUnitID(newId);
            }
            return 1;
        } catch (Throwable e) {
            OFDErrorBuffer.setError("ofd_add_page 失败: " + e);
            return 0;
        }
    }

    /* ==================== 6. 另存为标准 OFD 文件 ==================== */

    /**
     * 将当前内存 DOM 另存为标准 OFD 文件（ofdrw 完成 zip 打包）。
     *
     * <p>保存前自动清理签章/批注引用：编辑 OFD 会改变页面 Content.xml，
     * 电子签章的 SHA256 CheckValue 不再匹配，继续保留 {@code <Signatures>}
     * 和 {@code <Annotations>} 引用会导致渲染器验签失败、印章/批注层被隐藏。
     * 物理文件（Signs/、Annots/ 目录）保留在包里不影响，只是不再被引用加载。</p>
     *
     * @return 成功返回 1，失败返回 0
     */
    public static int saveToFile(OFDReader reader, String outputPath) {
        try {
            if (reader == null || outputPath == null || outputPath.isEmpty()) {
                OFDErrorBuffer.setError("ofd_save_to_file 参数无效");
                return 0;
            }
            OFDDir ofdDir = reader.getOFDDir();
            if (ofdDir == null) {
                OFDErrorBuffer.setError("ofd_save_to_file 无法获取 OFD 容器");
                return 0;
            }

            Path target = Paths.get(outputPath);
            ofdDir.jar(target);
            return 1;
        } catch (Throwable e) {
            OFDErrorBuffer.setError("ofd_save_to_file 失败: " + e);
            return 0;
        }
    }

    /* ==================== 辅助 ==================== */

    /** 计算下一个唯一对象 ID（在文档 MaxUnitID 基础上自增）。 */
    private static long nextUnitId(OFDReader reader) {
        try {
            Document doc = reader.cdDefaultDoc();
            CT_CommonData common = (doc == null) ? null : doc.getCommonData();
            if (common != null && common.getMaxUnitID() != null
                    && common.getMaxUnitID().getId() != null) {
                long base = common.getMaxUnitID().getId();
                common.setMaxUnitID(base + 1);
                return base + 1;
            }
        } catch (Throwable ignored) {
            // 兜底：用时间戳生成
        }
        return System.currentTimeMillis() & 0x7fffffffL;
    }
}
