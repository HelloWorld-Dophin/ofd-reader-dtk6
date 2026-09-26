package com.oftdrw.ffi.impl;

import java.awt.image.BufferedImage;
import java.io.ByteArrayOutputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.Arrays;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;

import javax.imageio.ImageIO;

import org.ofdrw.core.basicStructure.pageObj.Content;
import org.ofdrw.core.basicStructure.pageObj.Page;
import org.ofdrw.core.basicStructure.pageObj.layer.CT_Layer;
import org.ofdrw.core.basicStructure.pageObj.layer.PageBlockType;
import org.ofdrw.core.basicStructure.pageObj.layer.block.ImageObject;
import org.ofdrw.core.basicStructure.pageObj.layer.block.PathObject;
import org.ofdrw.core.basicStructure.pageObj.layer.block.TextObject;
import org.ofdrw.core.pageDescription.CT_GraphicUnit;
import org.ofdrw.core.pageDescription.color.color.CT_Color;
import org.ofdrw.core.text.TextCode;
import org.ofdrw.reader.OFDReader;
import org.ofdrw.reader.ResourceManage;

import com.sun.jna.Native;
import com.sun.jna.Pointer;

import com.oftdrw.ffi.api.OFDErrorBuffer;
import com.oftdrw.ffi.api.OfdStructs;

/**
 * OFD 页面元素解析器。
 *
 * <p>复用 ofdrw 底层能力，遍历指定页面的 Content -&gt; Layer -&gt; Block，
 * 完整提取文本（TextObject）、图片（ImageObject）、路径（PathObject）三大类对象，
 * 并透传全部参数（CTM 变换矩阵、描边宽度、路径指令、ARGB 颜色、资源二进制等）
 * 到 FFI 结构体 {@link OfdStructs}。</p>
 *
 * <p>内存约定：本类分配的所有 FFI 内存一律使用裸指针 {@link Native#malloc(long)}
 * 包装为 {@link Pointer}，由 {@link Native#free(long)} 释放，彻底绕开 JNA
 * {@link com.sun.jna.Memory} 的引用跟踪与 GC finalizer，避免二次释放导致的 SIGSEGV。
 * 全部分配登记在 {@link #ELEMENT_ALLOCATIONS}，由 {@code ofd_free_elements} 统一释放。</p>
 */
public final class OFDPageParser {

    private OFDPageParser() {
        // 工具类，禁止实例化
    }

    /** 容器指针 -> 该容器持有且需释放的全部 FFI 内存块（裸指针包装的 Pointer）。 */
    private static final Map<Pointer, List<Pointer>> ELEMENT_ALLOCATIONS = new ConcurrentHashMap<>();

    /**
     * 解析指定页面，返回页面全部元素容器（FFI 结构体）的内存指针。
     *
     * @param reader    OFD 文档读取器（由句柄持有）
     * @param pageIndex 页面索引（0 起始）
     * @return 承载文本/图片/路径的容器指针；参数无效或解析失败时返回 null
     */
    public static Pointer parsePage(OFDReader reader, int pageIndex) {
        if (reader == null || pageIndex < 0) {
            return null;
        }
        System.err.printf("[parsePage] pageIndex=%d (1-start=%d)%n", pageIndex, pageIndex + 1);

        List<Pointer> allocs = new ArrayList<>();
        List<Pointer> textPtrs = new ArrayList<>();
        List<Pointer> imagePtrs = new ArrayList<>();
        List<Pointer> pathPtrs = new ArrayList<>();
        java.util.Set<String> seenObjIds = new java.util.HashSet<>();

        try {
            // ofdrw 的 getPage 页码为 1 起始，而 FFI 页面索引为 0 起始
            Page page = reader.getPage(pageIndex + 1);
            if (page != null) {
                // ★ 获取主页面尺寸 —— Appearance Y 需要从 OFD 左下角坐标翻转为左上角
                double mainPageWidth = 210.0;  // 增值税普票默认值
                double mainPageHeight = 140.0; // 增值税普票默认值
                try {
                    org.ofdrw.core.basicType.ST_Box ps = reader.getPageSize(page);
                    if (ps != null) {
                        if (ps.getWidth() != null && ps.getWidth() > 0) mainPageWidth = ps.getWidth();
                        if (ps.getHeight() != null && ps.getHeight() > 0) mainPageHeight = ps.getHeight();
                    }
                } catch (Throwable ignored) {}

                // 主内容层
                Content content = page.getContent();

                // ★ 关键修复：OFDReader 的 FillColor 类会吃掉 AxialShd 子元素！
                // 必须直接读原始 Content.xml 的 dom4j Document！
                java.util.Map<String, org.dom4j.Element> gradMap = new java.util.HashMap<>();
                try {
                    // reader.getWorkDir() 返回解压后的临时目录
                    java.nio.file.Path workDir = reader.getWorkDir();
                    // parsePage 的 pageIndex 是 0-start → 对应 OFD 里的 Page_{pageIndex}
                    // reader.getPage(pageIndex+1) 返回这个 Page 对象
                    // 路径：Doc_0/Pages/Page_{pageIndex}/Content.xml
                    String contentPath = "Doc_0/Pages/Page_" + pageIndex + "/Content.xml";
                    java.io.File contentFile = workDir.resolve(contentPath).toFile();
                    if (contentFile.exists()) {
                        org.dom4j.io.SAXReader sax = new org.dom4j.io.SAXReader();
                        org.dom4j.Document doc = sax.read(contentFile);
                        collectGradientsFromXml(doc.getRootElement(), gradMap);
                        System.err.printf("[GradientMap] page=%d (%s) 大小=%d%n", pageIndex, contentPath, gradMap.size());
                    } else {
                        System.err.printf("[GradientMap] 文件不存在: %s (workDir=%s)%n", contentPath, workDir);
                    }
                } catch (Throwable t) {
                    System.err.println("[GradientMap] error: " + t.getMessage());
                }

                collectBlocks(content, textPtrs, imagePtrs, pathPtrs, reader, allocs, gradMap, seenObjIds);

                // 模板层（包含表格线、表头等共享元素）
                try {
                    List<org.ofdrw.core.basicStructure.pageObj.Template> tpls = page.getTemplates();
                    if (tpls != null) {
                        for (org.ofdrw.core.basicStructure.pageObj.Template tpl : tpls) {
                            if (tpl == null) continue;
                            try {
                                org.ofdrw.core.basicType.ST_RefID tplIdObj = tpl.getTemplateID();
                                if (tplIdObj != null) {
                                    String tplId = tplIdObj.toString();
                                    org.ofdrw.reader.model.TemplatePageEntity entity = reader.getTemplate(tplId);
                                    if (entity != null) {
                                        Page tplPage = entity.getPage();
                                        if (tplPage != null) {
                                            Content tc = tplPage.getContent();
                                            collectBlocks(tc, textPtrs, imagePtrs, pathPtrs, reader, allocs, null, seenObjIds);
                                        }
                                    }
                                }
                            } catch (Throwable ignored) {}
                        }
                    }
                } catch (Throwable ignored) {}

                collectSignatureElements(reader, pageIndex + 1, mainPageWidth, mainPageHeight,
                    textPtrs, imagePtrs, pathPtrs, allocs);

                System.err.printf("[parsePage_DONE] page=%d seenIds=%d texts=%d paths=%d imgs=%d%n",
                    pageIndex, seenObjIds.size(), textPtrs.size(), pathPtrs.size(), imagePtrs.size());
            }
        } catch (Throwable t) {
            // 解析失败：释放已分配内存，真实异常写入错误缓冲
            releaseAll(allocs);
            OFDErrorBuffer.setError("ofd_read_page_elements 解析页面失败: " + t);
            return null;
        }

        return buildContainer(textPtrs, imagePtrs, pathPtrs, allocs);
    }

    /**
     * 释放容器及其全部内部内存。
     *
     * @param containerPtr 由 {@link #parsePage} 返回的容器指针
     */
    public static void freeElements(Pointer containerPtr) {
        if (containerPtr == null) {
            return;
        }
        List<Pointer> allocs = ELEMENT_ALLOCATIONS.remove(containerPtr);
        if (allocs != null) {
            releaseAll(allocs);
        }
    }

    /** 释放一批 FFI 裸指针内存。 */
    private static void releaseAll(List<Pointer> allocs) {
        for (Pointer p : allocs) {
            try {
                if (p != null) {
                    Native.free(Pointer.nativeValue(p));
                }
            } catch (Throwable ignored) {
                // 释放异常不影响其他块回收
            }
        }
        allocs.clear();
    }

    /**
     * 解析签章元素（OFD appearance 包），把内部的 Text/Path/Image
     * 作为额外元素叠加到主页面，坐标平移到签章 Boundary 位置。
     * 这样签章文字可以被点击命中、复制、编辑。
     */
    private static void collectSignatureElements(OFDReader reader, int pageNo, double mainPageWidth, double mainPageHeight,
                                                  List<Pointer> textPtrs,
                                                  List<Pointer> imagePtrs,
                                                  List<Pointer> pathPtrs,
                                                  List<Pointer> allocs) {
        try {
            List<org.ofdrw.reader.model.StampAnnotEntity> stamps = reader.getStampAnnots();
            if (stamps == null) return;

            // ==== 预读 Annotation.xml 里当前页面的所有 Stamp Annot Appearance Boundary ====
            // Signature.xml 的 StampAnnot Boundary 是签名引用位置（错误！如 90,8），
            // Annotation.xml 的 Appearance Boundary 是预留渲染区域（如 4.5 104 115 20）
            // 公章 nested OFD 有自己的页面大小（如 30×20），需要在 Appearance 区域内居中放置
            Double appearanceBx = null, appearanceBy = null;
            double appearanceBw = 0.0, appearanceBh = 0.0;
            try {
                List<org.ofdrw.reader.model.AnnotionEntity> annots = reader.getAnnotationEntities();
                if (annots != null) {
                    for (org.ofdrw.reader.model.AnnotionEntity ae : annots) {
                        if (ae == null) continue;
                        // pageId 可能是 "1"（从1开始）或内部 ID，做宽松匹配
                        String pid = ae.getPageId();
                        boolean pageMatch = false;
                        if (pid != null) {
                            try { pageMatch = Integer.parseInt(pid) == pageNo; }
                            catch (NumberFormatException ignored) {}
                            if (!pageMatch) pageMatch = pid.equals(String.valueOf(pageNo));
                        }
                        if (!pageMatch && pageNo == 1 && (pid == null || pid.equals("1"))) {
                            pageMatch = true; // 默认 page 1 兜底
                        }
                        if (!pageMatch) continue;

                        List<org.ofdrw.core.annotation.pageannot.Annot> list = ae.getAnnots();
                        if (list == null) continue;
                        for (org.ofdrw.core.annotation.pageannot.Annot a : list) {
                            if (a == null) continue;
                            // 找 Type=Stamp 的 Annot
                            Object type = a.getType();
                            String typeStr = type != null ? type.toString() : "";
                            if (!typeStr.contains("Stamp") && !typeStr.contains("STAMP")) continue;

                            org.ofdrw.core.annotation.pageannot.Appearance app = a.getAppearance();
                            if (app == null) continue;
                            org.ofdrw.core.basicType.ST_Box b = app.getBoundary();
                            if (b == null) continue;
                            Double ax = b.getTopLeftX();
                            Double ay = b.getTopLeftY();
                            Double aw = b.getWidth();
                            Double ah = b.getHeight();
                            if (ax == null || ay == null) continue;
                            // ★ 关键：Appearance 是预留区域，公章需要在区域内居中
                            // 先记录原始 Appearance Boundary，等下拿到 nested OFD 大小后再居中
                            appearanceBx = ax;
                            appearanceBy = ay;
                            appearanceBw = aw != null ? aw : 0.0;
                            appearanceBh = ah != null ? ah : 0.0;
                            System.err.println("[Stamp] Annotation.xml Appearance Boundary=("
                                + ax + "," + ay + "," + aw + "x" + ah + ")");
                        }
                    }
                }
            } catch (Throwable t) {
                System.err.println("[Stamp] Annotation lookup failed (ok): " + t.getMessage());
            }

            for (org.ofdrw.reader.model.StampAnnotEntity entity : stamps) {
                if (entity == null) continue;
                try {
                    byte[] rawBytes = entity.getImageByte();
                    if (rawBytes == null || rawBytes.length == 0) continue;

                    // 判断类型
                    String imgType = entity.getImgType();
                    System.err.println("[Stamp] imgType=" + imgType + " bytes=" + rawBytes.length
                        + " appearance=(" + appearanceBx + "," + appearanceBy + "," + appearanceBw + "x" + appearanceBh + ")");

                    if ("ofd".equalsIgnoreCase(imgType)
                            || (rawBytes.length >= 4 && rawBytes[0] == 'P' && rawBytes[1] == 'K')) {
                        // 嵌套 OFD → 解压 → 打开 → 解析元素 + 居中放置到 Appearance 区域
                        overlayStampOfd(rawBytes, appearanceBx, appearanceBy, appearanceBw, appearanceBh,
                            mainPageWidth, mainPageHeight, textPtrs, imagePtrs, pathPtrs, allocs);
                    }
                } catch (Throwable t) {
                    System.err.println("[Stamp] entity 异常: " + t);
                }
            }
        } catch (Throwable ignored) {}
    }

    /**
     * 解压签章嵌套 OFD → 用 OFDReader 打开第一页 → 把 Text/Path/Image
     * 叠加到主页面，**在 Appearance Boundary 区域内居中放置**。
     */
    private static void overlayStampOfd(byte[] zipBytes,
                                        Double appX, Double appY,
                                        double appW, double appH,
                                        double mainPageWidth, double mainPageHeight,
                                        List<Pointer> textPtrs,
                                        List<Pointer> imagePtrs,
                                        List<Pointer> pathPtrs,
                                        List<Pointer> allocs) {
        try {
            // 解压到临时 byte[] zip，然后给 OFDReader 打开
            byte[] innerOfd = null;
            try (java.io.ByteArrayInputStream bis = new java.io.ByteArrayInputStream(zipBytes);
                 java.util.zip.ZipInputStream zis = new java.util.zip.ZipInputStream(bis)) {
                java.util.zip.ZipEntry entry;
                while ((entry = zis.getNextEntry()) != null) {
                    // OFD.xml 是入口，解压整个包为 byte[]（用 OFDReader 自己能处理的格式）
                    if ("OFD.xml".equalsIgnoreCase(entry.getName())) {
                        // 我们需要整个 ZIP 包给 OFDReader
                        // 不 break，读完所有
                    }
                    zis.closeEntry();
                }
            }

            // OFDReader 可以直接从 ZIP byte[] 打开（它本身 OFD 就是 ZIP）
            // 把 stamp ZIP 写成 ByteArrayInputStream
            java.io.ByteArrayInputStream bais = new java.io.ByteArrayInputStream(zipBytes);
            // 注意：OFDReader 构造器接受 Path，我们需要走 Reader 的 open 逻辑
            // 用 org.ofdrw.reader.OFDReader 的 InputStream 构造器
            org.ofdrw.reader.OFDReader stampReader;
            try {
                stampReader = new org.ofdrw.reader.OFDReader(bais);
            } catch (NoSuchMethodError nsme) {
                // 没有 InputStream 构造器 → 写临时文件
                System.err.println("[Stamp] no InputStream ctor, fallback tmp file");
                java.io.File tmp = java.io.File.createTempFile("stamp_", ".ofd");
                try (java.io.FileOutputStream fos = new java.io.FileOutputStream(tmp)) {
                    fos.write(zipBytes);
                }
                stampReader = new org.ofdrw.reader.OFDReader(tmp.toPath());
                tmp.deleteOnExit();
            }

            if (stampReader == null) return;
            int pageCount = stampReader.getNumberOfPages();
            System.err.println("[Stamp] nested OFD pages=" + pageCount);
            if (pageCount <= 0) { stampReader.close(); return; }

            // 解析第一页（签章只有一页）
            Page stampPage = stampReader.getPage(1);
            if (stampPage != null) {
                org.ofdrw.core.basicType.ST_Box stampPageSize = stampReader.getPageSize(stampPage);
                System.err.println("[Stamp] stampPageSize: " + stampPageSize);
                double stampW = stampPageSize != null ? stampPageSize.getWidth() : 30.0;
                double stampH = stampPageSize != null ? stampPageSize.getHeight() : 20.0;

                // ★ Appearance Boundary Y 是 OFD 原生左下角原点坐标（OfdRW 不翻 Annotation）
                // ST_Box.getTopLeftY() 返回的是框左下角 Y（原点在页面底部）
                // 框的左上角 Y（左上角原点）= pageHeight - nativeY - boxHeight
                // 例: appY=104, appH=20 → flippedTop = 140-104-20 = 16mm (从顶部算)
                double flippedAppTop = (appY != null ? mainPageHeight - appY - appH : 0.0);

                // ★ 水平位置：Appearance 框太宽(115mm)覆盖近半页，其中心(62mm)≠页面中心(105mm)
                // 数科OFD公章盖在标题正下方=页面水平居中！用 mainPageWidth 居中
                // 水平居中：公章中心 = 页面中心
                double finalStampX = (mainPageWidth - stampW) / 2.0;
                // 垂直居中：在翻转后的 Appearance 框内
                double finalStampY = flippedAppTop;
                if (appH > 0 && stampH > 0) finalStampY += (appH - stampH) / 2.0;
                System.err.printf("[Stamp] centered: pageW=%.1f pageH=%.1f app(%.1f,%.1f→flipTop=%.1f,%.0fx%.0f) stamp(%.0fx%.0f) → final(%.2f,%.2f)%n",
                    mainPageWidth, mainPageHeight,
                    (appX != null ? appX : 0), (appY != null ? appY : 0), flippedAppTop, appW, appH,
                    stampW, stampH, finalStampX, finalStampY);

                // 只解析公章主层，跳过模板层（公章嵌套 OFD 一般没有独立模板，避免重复渲染）
                int textBefore = textPtrs.size();
                int pathBefore = pathPtrs.size();
                int imgBefore  = imagePtrs.size();
                collectStampBlocks(stampPage.getContent(), stampReader,
                    finalStampX, finalStampY, stampH, textPtrs, imagePtrs, pathPtrs, allocs);
                System.err.println("[Stamp] main layer added: text="
                    + (textPtrs.size()-textBefore) + " path=" + (pathPtrs.size()-pathBefore)
                    + " img=" + (imagePtrs.size()-imgBefore));
            } else {
                System.err.println("[Stamp] stampPage is NULL!");
            }
            stampReader.close();
        } catch (Throwable t) {
            System.err.println("[Stamp] overlayStampOfd FAILED: " + t);
            t.printStackTrace(System.err);
        }
    }

    /**
     * 签章专用 collectBlocks：额外把每个元素的 CTM translate(e,f)
     * 叠加 stampX/stampY，让签章元素落在主页面的签章位置。
     */
    private static void collectStampBlocks(Content content, org.ofdrw.reader.OFDReader reader,
                                            double stampX, double stampY, double stampPageHeight,
                                            List<Pointer> textPtrs,
                                            List<Pointer> imagePtrs,
                                            List<Pointer> pathPtrs,
                                            List<Pointer> allocs) {
        if (content == null) return;
        try {
            List<CT_Layer> layers = content.getLayers();
            if (layers == null) return;
            for (CT_Layer layer : layers) {
                if (layer == null) continue;
                try {
                    List<PageBlockType> blocks = layer.getPageBlocks();
                    if (blocks == null) continue;
                    for (PageBlockType block : blocks) {
                        if (block == null) continue;
                        try {
                            if (block instanceof TextObject) {
                                List<Pointer> ps = buildTextCodes((TextObject) block, reader, allocs, null);
                                if (ps != null) {
                                    for (Pointer p : ps) {
                                        double a = p.getDouble(OfdStructs.TEXT_CTM_OFF + 0 * 8);
                                        double b = p.getDouble(OfdStructs.TEXT_CTM_OFF + 1 * 8);
                                        double c = p.getDouble(OfdStructs.TEXT_CTM_OFF + 2 * 8);
                                        double d = p.getDouble(OfdStructs.TEXT_CTM_OFF + 3 * 8);
                                        double e = p.getDouble(OfdStructs.TEXT_CTM_OFF + 4 * 8);
                                        double f = p.getDouble(OfdStructs.TEXT_CTM_OFF + 5 * 8);
                                        // DEBUG: 公章 text 原始 CTM
                                        System.err.println(String.format("[Stamp.Text.raw] ctm=[%.2f %.2f %.2f %.2f %.2f %.2f]",
                                            a, b, c, d, e, f));
                                        // ofdrw 已对嵌套 OFD 做过 y 翻转，只做简单 translate 叠加
                                        p.setDouble(OfdStructs.TEXT_CTM_OFF + 4 * 8, e + stampX);
                                        p.setDouble(OfdStructs.TEXT_CTM_OFF + 5 * 8, f + stampY);
                                    }
                                    textPtrs.addAll(ps);
                                }
                            } else if (block instanceof PathObject) {
                                Pointer p = buildPath((PathObject) block, allocs, null);
                                if (p != null) {
                                    double a = p.getDouble(OfdStructs.PATH_CTM_OFF + 0 * 8);
                                    double b = p.getDouble(OfdStructs.PATH_CTM_OFF + 1 * 8);
                                    double c = p.getDouble(OfdStructs.PATH_CTM_OFF + 2 * 8);
                                    double d = p.getDouble(OfdStructs.PATH_CTM_OFF + 3 * 8);
                                    double tx = p.getDouble(OfdStructs.PATH_CTM_OFF + 4 * 8);
                                    double ty = p.getDouble(OfdStructs.PATH_CTM_OFF + 5 * 8);
                                    System.err.println(String.format("[Stamp.Path.raw] ctm=[%.2f %.2f %.2f %.2f %.2f %.2f]",
                                        a, b, c, d, tx, ty));
                                    // ofdrw 已翻 y → 只叠加边界 translate，不改 scale
                                    p.setDouble(OfdStructs.PATH_CTM_OFF + 4 * 8, tx + stampX);
                                    p.setDouble(OfdStructs.PATH_CTM_OFF + 5 * 8, ty + stampY);

                                    // debug + 默认红色 stroke
                                    int stroke = p.getInt(OfdStructs.PATH_STROKE_OFF);
                                    int fill   = p.getInt(OfdStructs.PATH_FILL_OFF);
                                    System.err.println(String.format("[Stamp.Path] ctm=[%.2f %.2f %.2f %.2f %.2f %.2f] stroke=0x%08x fill=0x%08x",
                                        a, b, c, d, tx + stampX, ty + stampY, stroke, fill));
                                    if (stroke == 0x00000000 && fill == 0x00000000) {
                                        p.setInt(OfdStructs.PATH_STROKE_OFF, 0xFFFF0000);
                                    }
                                    pathPtrs.add(p);
                                }
                            } else if (block instanceof ImageObject) {
                                Pointer p = buildImage((ImageObject) block, reader, allocs);
                                if (p != null) {
                                    double tx = p.getDouble(OfdStructs.IMAGE_CTM_OFF + 4 * 8);
                                    double ty = p.getDouble(OfdStructs.IMAGE_CTM_OFF + 5 * 8);
                                    p.setDouble(OfdStructs.IMAGE_CTM_OFF + 4 * 8, tx + stampX);
                                    p.setDouble(OfdStructs.IMAGE_CTM_OFF + 5 * 8, ty + stampY);
                                    imagePtrs.add(p);
                                }
                            }
                        } catch (Throwable ignored) {}
                    }
                } catch (Throwable ignored) {}
            }
        } catch (Throwable ignored) {}
    }

    /** 从一个 Content 对象收集全部 Text/Image/Path 块。 */
    /**
     * 遍历原始 Content.xml，建立 ID → 渐变 FillColor Element 的映射。
     * OFDRW 的 FillColor 类会吃掉 AxialShd/RadialShd 子元素，
     * 所以必须从原始 XML 里直接读！
     */
    private static void collectGradientsFromXml(org.dom4j.Element root,
            java.util.Map<String, org.dom4j.Element> gradMap) {
        String tag = root.getName();
        if ("FillColor".equals(tag)) {
            org.dom4j.Element shd = root.element("AxialShd");
            if (shd == null) shd = root.element("RadialShd");
            if (shd != null) {
                // 往上找 ID，但必须验证祖先类型是 TextObject/PathObject！
                // 因为 OFD XML 有时 FillColor 没正确闭合，dom4j 会把后面元素嵌套进来
                org.dom4j.Element cur = root.getParent();
                while (cur != null) {
                    String ctag = cur.getName();
                    String id = cur.attributeValue("ID");
                    // 只接受 TextObject 或 PathObject 作为有效的渐变宿主！
                    if (id != null && !id.isEmpty()
                        && (ctag.equals("TextObject") || ctag.equals("PathObject"))) {
                        gradMap.put(id, root);
                        System.err.printf("[GRAD_SCAN] FillColor→%s ID=%s ✅ 已存 gradMap%n", ctag, id);
                        break;
                    }
                    if (ctag.equals("Page") || ctag.equals("Layer") || ctag.equals("Content")
                        || ctag.equals("TextObject") || ctag.equals("PathObject")) {
                        // 到达边界或已经是宿主元素但没 ID，停！
                        if (id == null || id.isEmpty()) {
                            System.err.printf("[GRAD_SCAN] FillColor→%s (无有效ID) 停%n", ctag);
                        }
                        break;
                    }
                    cur = cur.getParent();
                }
                if (cur == null) {
                    System.err.println("[GRAD_SCAN] FillColor→无有效TextObject/PathObject祖先! 跳过");
                }
            }
        }
        for (Object ch : root.elements()) {
            collectGradientsFromXml((org.dom4j.Element) ch, gradMap);
        }
    }

    private static void collectBlocks(Content content,
                                      List<Pointer> textPtrs,
                                      List<Pointer> imagePtrs,
                                      List<Pointer> pathPtrs,
                                      OFDReader reader,
                                      List<Pointer> allocs,
                                      java.util.Map<String, org.dom4j.Element> gradMap,
                                      java.util.Set<String> seenObjIds) {
        if (content == null) return;
        System.err.println("[collectBlocks] ENTRY seenIds=" + seenObjIds.size() + " existing texts=" + textPtrs.size());
        try {
            List<CT_Layer> layers = content.getLayers();
            System.err.println("[collectBlocks] layers=" + (layers != null ? layers.size() : "NULL"));
            if (layers == null) return;
            int blockCount = 0;
            for (CT_Layer layer : layers) {
                if (layer == null) continue;
                try {
                    List<PageBlockType> blocks = layer.getPageBlocks();
                    if (blocks == null) continue;
                    blockCount += blocks.size();
                    processBlocks(blocks, textPtrs, imagePtrs, pathPtrs, reader, allocs, gradMap, seenObjIds);
                } catch (Throwable ignored) {}
            }
        } catch (Throwable ignored) {}
    }

    /** 处理一批 PageBlockType（叶子递归时复用）。 */
    private static void processBlocks(List<PageBlockType> blocks,
                                      List<Pointer> textPtrs,
                                      List<Pointer> imagePtrs,
                                      List<Pointer> pathPtrs,
                                      OFDReader reader,
                                      List<Pointer> allocs,
                                      java.util.Map<String, org.dom4j.Element> gradMap,
                                      java.util.Set<String> seenObjIds) {
        if (blocks == null) return;
        int txtBefore = textPtrs.size();
        int dupSkip = 0;
        for (PageBlockType block : blocks) {
            if (block == null) continue;
            try {
                String objId = null;
                try {
                    if (!(block instanceof org.ofdrw.core.basicStructure.pageObj.layer.block.CT_PageBlock)) {
                        java.lang.reflect.Method m = block.getClass().getMethod("getID");
                        Object idObj = m.invoke(block);
                        objId = (idObj != null) ? idObj.toString() : "NULL";
                    }
                } catch (Throwable t) {
                    objId = "ERR:" + t.getMessage();
                }

                // ★ 所有 TextObject 打印 objId + seen
                if (block instanceof TextObject) {
                    boolean firstTime = (objId == null || !seenObjIds.contains(objId));
                    System.err.println("[BLK] TextObject id=" + objId + " seenIds.size=" + seenObjIds.size() + " firstTime=" + firstTime);
                }

                if (objId != null && !objId.startsWith("ERR") && !seenObjIds.add(objId)) {
                    dupSkip++;
                    if (dupSkip < 5) System.err.println("[DUP_SKIP] id=" + objId);
                    continue;
                }

                if (block instanceof TextObject) {
                    List<Pointer> ps = buildTextCodes((TextObject) block, reader, allocs, gradMap);
                    if (ps != null) textPtrs.addAll(ps);
                } else if (block instanceof ImageObject) {
                    Pointer p = buildImage((ImageObject) block, reader, allocs);
                    if (p != null) imagePtrs.add(p);
                } else if (block instanceof PathObject) {
                    Pointer p = buildPath((PathObject) block, allocs, gradMap);
                    if (p != null) pathPtrs.add(p);
                } else if (block instanceof org.ofdrw.core.basicStructure.pageObj.layer.block.CT_PageBlock) {
                    List<PageBlockType> inner =
                        ((org.ofdrw.core.basicStructure.pageObj.layer.block.CT_PageBlock) block)
                            .getPageBlocks();
                    processBlocks(inner, textPtrs, imagePtrs, pathPtrs, reader, allocs, gradMap, seenObjIds);
                }
            } catch (Throwable ignored) {}
        }
    }

    /* ==================== 元素字节构建 ==================== */

    /**
     * 按 TextCode 拆分：一个 TextObject → N 个 OfdTextItem。
     * 每个 TextCode 有独立的 X/Y 偏移（DeltaX/DeltaY），对应 OFD 里的一行/一段。
     */
    private static List<Pointer> buildTextCodes(TextObject text, OFDReader reader, List<Pointer> allocs,
            java.util.Map<String, org.dom4j.Element> gradMap) {
        if (text == null) return null;

        List<TextCode> codes = text.getTextCodes();
        if (codes == null || codes.isEmpty()) return null;

        double size = (text.getSize() == null) ? 0.0 : text.getSize();
        int argb = colorToArgb(text.getFillColor(), 0xFF000000);
        // ★ 用原始 XML 的 gradMap 拿渐变！OFDReader 的 FillColor 会吃掉 AxialShd！
        // ★ 先拿 Boundary 用于渐变归一化到 ObjectBoundingMode 0-1
        double bX = 0, bY = 0, bW = 1, bH = 1;
        try {
            if (text.getBoundary() != null) {
                bX = text.getBoundary().getTopLeftX();
                bY = text.getBoundary().getTopLeftY();
                bW = text.getBoundary().getWidth();
                bH = text.getBoundary().getHeight();
            }
        } catch (Throwable ignored) {}
        Pointer gradPtr = null;
        try {
            // ★ 关键：OFD XML 属性是 ID（不是 ObjID），必须用 getID()！
            String objId = null;
            try {
                java.lang.reflect.Method m = text.getClass().getMethod("getID");
                Object idObj = m.invoke(text);
                objId = (idObj != null) ? idObj.toString() : null;
            } catch (Throwable ignored) {}
            if (objId != null && gradMap != null) {
                org.dom4j.Element fc = gradMap.get(objId);
                if (fc != null) {
                    gradPtr = parseGradient(fc, allocs, bX, bY, bW, bH);
                    System.err.printf("[GRAD_HIT] TextObject ID=%s ptr=%s%n", objId, gradPtr);
                }
            }
        } catch (Throwable ignored) {}
        double[] ctm = ctmToArray(text);

        // DEBUG: 打印公章文字原始 fontSize + StrokeColor
        Double strokeW = text.getLineWidth();
        CT_Color strokeColor = text.getStrokeColor();
        int strokeArgb = strokeColor != null ? colorToArgb(strokeColor, 0xFF000000) : 0x00000000;
        if (strokeW != null && strokeW > 0) {
            for (TextCode tc : codes) {
                String content = tc.getContent() != null ? tc.getContent() : "";
                if (content.length() > 0) {
                    System.err.printf("[Text.stroke] Content=[%s] Size=%.4f StrokeW=%.4f StrokeColor=0x%08x FillColor=0x%08x%n",
                        content, size, strokeW, strokeArgb, argb);
                }
            }
        } else {
            for (TextCode tc : codes) {
                String content = tc.getContent() != null ? tc.getContent() : "";
                if (content.length() > 0 && (ctm[0] < 0.99 || Math.abs(ctm[1]) > 0.01 || Math.abs(ctm[2]) > 0.01 || ctm[3] < 0.99)) {
                    System.err.printf("[Stamp.Text.size=%.4f NO-STROKE] Content=[%s] CTM=[%.2f %.2f %.2f %.2f %.2f %.2f]%n",
                        size, content, ctm[0], ctm[1], ctm[2], ctm[3], ctm[4], ctm[5]);
                }
            }
        }

        // 检测竖向文本：Direction != Angle_0 或 boundary 宽 < 高
        org.ofdrw.core.text.text.Direction readDir = text.getReadDirection();
        org.ofdrw.core.text.text.Direction charDir = text.getCharDirection();
        int readAngle = (readDir != null) ? readDir.ordinal() * 90 : 0; // 0=Angle_0, 1=Angle_90...
        int charAngle = (charDir != null) ? charDir.ordinal() * 90 : 0;
        boolean boundaryVertical = false;
        double bw = 0, bh = 0, bx = 0, by = 0;
        try {
            if (text.getBoundary() != null) {
                bw = text.getBoundary().getWidth();
                bh = text.getBoundary().getHeight();
                bx = text.getBoundary().getTopLeftX();
                by = text.getBoundary().getTopLeftY();
                if (bw > 0 && bh > 0 && bw < bh) boundaryVertical = true;
            }
        } catch (Throwable ignored) {}
        boolean isVertical = (readAngle != 0 && readAngle != 180) || boundaryVertical;

        // 竖向文本：不旋转 CTM，改为 content 拆成单字 + Y 偏移递增
        // 例："购买方" → TextCode1("购", y=0) + TextCode2("买", y=size*1) + TextCode3("方", y=size*2)
        if (isVertical) {
            // 重置 CTM 为 identity + boundary 位移
            double cx = bx + bw / 2.0; // 仅用于 debug
            ctm = new double[]{1.0, 0.0, 0.0, 1.0, bx, by};
        }

        System.err.println("[TextObject] codes=" + codes.size() + " size=" + size + " argb=0x"
                          + Integer.toHexString(argb) + " boundary=("
                          + text.getBoundary() + ") ctm=["
                          + String.format("%.2f %.2f %.2f %.2f %.2f %.2f",
                              ctm[0],ctm[1],ctm[2],ctm[3],ctm[4],ctm[5]) + "]"
                          + " readDir=" + readDir + " charDir=" + charDir
                          + " readAngle=" + readAngle + " boundaryV=" + boundaryVertical
                          + " IS_VERTICAL=" + isVertical);

        int fontId = 0;
        String fontName = null;
        if (text.getFont() != null && text.getFont().getRefId() != null
                && text.getFont().getRefId().getId() != null) {
            fontId = text.getFont().getRefId().getId().intValue();
            fontName = lookupFontName(reader, fontId);
        }

        List<Pointer> result = new java.util.ArrayList<>();

        for (TextCode tc : codes) {
            if (tc == null || tc.getContent() == null) continue;
            String content = tc.getContent();
            if (content.isEmpty()) continue;

            Double dx = tc.getX();
            Double dy = tc.getY();
            double baseX = (dx != null) ? dx : 0.0;
            double baseY = (dy != null) ? dy : 0.0;

            // 解析 DeltaX，计算逐字位置 + 按负值拆行
            double contentAdvance = 0.0;
            // 每个字符的绝对 X 位置（在 CTM 本地坐标中）
            java.util.List<double[]> lineCharPositions = new java.util.ArrayList<>();  // 每行的 [charX0, charX1, ...]
            java.util.List<int[]> lineCharRanges = new java.util.ArrayList<>();      // 每行在 content 中的起止索引
            int totalChars = content.length();

            if (tc.getDeltaX() != null && tc.getDeltaX().size() > 0 && totalChars > 0) {
                try {
                    Double[] dxArr = tc.getDeltaX().toDouble();
                    if (dxArr != null && dxArr.length > 0) {
                        double[] curRowXs = new double[totalChars]; // 临时存每行
                        double curX = 0;
                        int rowStart = 0;
                        int rowCharCount = 0;
                        double rowAdv = 0;

                        // 第一个字符位置 = baseX（OFD TextCode.X）
                        curRowXs[0] = 0;  // 相对 baseX 的偏移
                        int rowCharIdx = 0;  // 当前行已处理字符数

                        for (int i = 0; i < dxArr.length && rowStart + rowCharIdx + 1 < totalChars; i++) {
                            Double d = dxArr[i];
                            if (d == null) d = 0.0;
                            if (d < 0) {
                                // 换行！当前行结束
                                rowCharIdx++;  // 这个 dx 对应的是第 rowCharIdx+1 个字符... 不对
                                // 换行 dx 对应的字符还没加入当前行，它是下一行的第一个
                                lineCharPositions.add(Arrays.copyOf(curRowXs, rowCharIdx));
                                lineCharRanges.add(new int[]{rowStart, rowStart + rowCharIdx - 1});
                                if (lineCharPositions.size() == 1) contentAdvance = rowAdv;
                                // 下一行
                                rowStart += rowCharIdx;
                                rowCharIdx = 0;
                                rowAdv = 0;
                                curX = 0;
                                curRowXs[0] = 0;
                                // d 是换行，不推进
                            } else {
                                curX += d;
                                rowAdv += d;
                                rowCharIdx++;
                                curRowXs[rowCharIdx] = curX;
                            }
                        }
                        // 最后一行
                        rowCharIdx++;
                        lineCharPositions.add(Arrays.copyOf(curRowXs, rowCharIdx));
                        lineCharRanges.add(new int[]{rowStart, rowStart + rowCharIdx - 1});
                        if (lineCharPositions.size() == 1) contentAdvance = rowAdv;
                    }
                } catch (Throwable t) {
                    System.err.println("[DeltaX parse err] " + t.getMessage());
                }
            }

            boolean dxAvailable = !lineCharPositions.isEmpty();
            System.err.println("[DeltaX] content=" + content.substring(0, Math.min(10, content.length()))
                + " totalChars=" + totalChars + " contentAdvance=" + contentAdvance
                + " dxAvailable=" + dxAvailable);

            // 竖向文本：拆成单字，每个字均匀分布在 boundary 高度内
            // CTM 已带 translate(bx,by)，TextCode.y 是 CTM 本地偏移（相对于 boundary 左上角）
            if (isVertical && content.length() > 1) {
                int n = content.length();
                // 均匀分布：n 个字占 bh 高度，两端留白
                double yStep = bh / (n + 1);
                double baseLineOffset = size * 0.25;
                for (int ci = 0; ci < n; ci++) {
                    String ch = content.substring(ci, ci + 1);
                    // 本地 y 偏移（相对 boundary 左上角）
                    double yy = (ci + 1) * yStep + baseLineOffset;

                    Pointer strPtr = writeString(ch);
                    allocs.add(strPtr);

                    Pointer fontNamePtr = null;
                    if (fontName != null && !fontName.isEmpty()) {
                        fontNamePtr = writeString(fontName);
                        allocs.add(fontNamePtr);
                    }

                    Pointer mem = new Pointer(Native.malloc(OfdStructs.TEXT_SIZE));
                    mem.setPointer(OfdStructs.TEXT_CONTENT_OFF, strPtr);
                    mem.setDouble(OfdStructs.TEXT_FONTSIZE_OFF, size);
                    mem.setInt(OfdStructs.TEXT_ARGB_OFF, argb);
                    for (int i = 0; i < OfdStructs.CTM_SIZE; i++) {
                        mem.setDouble(OfdStructs.TEXT_CTM_OFF + (long) i * 8, ctm[i]);
                    }
                    mem.setDouble(OfdStructs.TEXT_X_OFF, baseX);
                    mem.setDouble(OfdStructs.TEXT_Y_OFF, yy);
                    mem.setInt(OfdStructs.TEXT_FONTID_OFF, fontId);
                    mem.setDouble(OfdStructs.TEXT_BW_OFF, bw);
                    mem.setDouble(OfdStructs.TEXT_BH_OFF, bh);
                    mem.setDouble(OfdStructs.TEXT_ADV_OFF, 0.0);  // 单字不需要缩放
                    mem.setPointer(OfdStructs.TEXT_FONTNAME_OFF, fontNamePtr);
                    mem.setPointer(OfdStructs.TEXT_GRADIENT_OFF, gradPtr);
                    allocs.add(mem);
                    result.add(mem);
                }
                continue;
            }

            // 水平文本：如果有 DeltaX 精确位置，按 DeltaX 逐字绘制
            // 每个字符独立 TextItem，X 由 DeltaX 累加得到
            // 如果没有 DeltaX 或只有单字，退回到整行绘制
            // ★ 关键改进：有渐变的 TextObject 不拆字！整行绘制才能让渐变跨字连续！
            if (!isVertical && dxAvailable && totalChars > 1 && gradPtr == null) {
                // 计算行间距：优先 DeltaY，否则 fontSize * 1.3
                double lineGap = size * 1.3;
                if (tc.getDeltaY() != null && tc.getDeltaY().size() > 0) {
                    try {
                        Double[] dyArr = tc.getDeltaY().toDouble();
                        if (dyArr != null) {
                            for (Double d : dyArr) {
                                if (d != null && d > 0.1) { lineGap = d; break; }
                            }
                        }
                    } catch (Throwable ignored) {}
                }

                System.err.println("[Per-char draw] rows=" + lineCharPositions.size()
                    + " lineGap=" + lineGap);

                for (int li = 0; li < lineCharPositions.size(); li++) {
                    double[] rowXs = lineCharPositions.get(li);
                    int[] range = lineCharRanges.get(li);
                    double yy = baseY + li * lineGap;
                    for (int ci = 0; ci < rowXs.length && range[0] + ci < totalChars; ci++) {
                        char ch = content.charAt(range[0] + ci);
                        // 跳过代理对一半
                        if (Character.isHighSurrogate(ch) && ci + 1 < rowXs.length) {
                            continue;
                        }
                        String single = String.valueOf(ch);
                        if (Character.isHighSurrogate(ch) && ci + 1 < rowXs.length
                            && range[0] + ci + 1 < totalChars) {
                            single = single + content.charAt(range[0] + ci + 1);
                        }
                        Pointer strPtr = writeString(single);
                        allocs.add(strPtr);

                        Pointer fontNamePtr = null;
                        if (fontName != null && !fontName.isEmpty()) {
                            fontNamePtr = writeString(fontName);
                            allocs.add(fontNamePtr);
                        }

                        double charX = baseX + rowXs[ci];

                        Pointer mem = new Pointer(Native.malloc(OfdStructs.TEXT_SIZE));
                        mem.setPointer(OfdStructs.TEXT_CONTENT_OFF, strPtr);
                        mem.setDouble(OfdStructs.TEXT_FONTSIZE_OFF, size);
                        mem.setInt(OfdStructs.TEXT_ARGB_OFF, argb);
                        for (int i = 0; i < OfdStructs.CTM_SIZE; i++) {
                            mem.setDouble(OfdStructs.TEXT_CTM_OFF + (long) i * 8, ctm[i]);
                        }
                        mem.setDouble(OfdStructs.TEXT_X_OFF, charX);
                        mem.setDouble(OfdStructs.TEXT_Y_OFF, yy);
                        mem.setInt(OfdStructs.TEXT_FONTID_OFF, fontId);
                        mem.setDouble(OfdStructs.TEXT_BW_OFF, bw);
                        mem.setDouble(OfdStructs.TEXT_BH_OFF, bh);
                        mem.setDouble(OfdStructs.TEXT_ADV_OFF, 0.0);
                        mem.setPointer(OfdStructs.TEXT_FONTNAME_OFF, fontNamePtr);
                        mem.setPointer(OfdStructs.TEXT_GRADIENT_OFF, gradPtr);
                        allocs.add(mem);
                        result.add(mem);
                    }
                }
                continue;  // ★ 关键：跳到外层 for (TextCode tc : codes)，跳过整行绘制
            } else {
            System.err.println("[WHOLE_LINE] content=" + content.substring(0, Math.min(10, content.length())) + " baseXY=(" + baseX + "," + baseY + ")");
            Pointer strPtr = writeString(content);
            allocs.add(strPtr);

            Pointer fontNamePtr = null;
            if (fontName != null && !fontName.isEmpty()) {
                fontNamePtr = writeString(fontName);
                allocs.add(fontNamePtr);
            }

            Pointer mem = new Pointer(Native.malloc(OfdStructs.TEXT_SIZE));
            mem.setPointer(OfdStructs.TEXT_CONTENT_OFF, strPtr);
            mem.setDouble(OfdStructs.TEXT_FONTSIZE_OFF, size);
            mem.setInt(OfdStructs.TEXT_ARGB_OFF, argb);
            for (int i = 0; i < OfdStructs.CTM_SIZE; i++) {
                mem.setDouble(OfdStructs.TEXT_CTM_OFF + (long) i * 8, ctm[i]);
            }
            mem.setDouble(OfdStructs.TEXT_X_OFF, baseX);
            mem.setDouble(OfdStructs.TEXT_Y_OFF, baseY);
            mem.setInt(OfdStructs.TEXT_FONTID_OFF, fontId);
            mem.setDouble(OfdStructs.TEXT_BW_OFF, bw);
            mem.setDouble(OfdStructs.TEXT_BH_OFF, bh);
            mem.setDouble(OfdStructs.TEXT_ADV_OFF, contentAdvance);
            mem.setPointer(OfdStructs.TEXT_FONTNAME_OFF, fontNamePtr);
            mem.setPointer(OfdStructs.TEXT_GRADIENT_OFF, gradPtr);
            allocs.add(mem);
            result.add(mem);
            }  // ← 关闭 else
        }

        return result.isEmpty() ? null : result;
    }

    /** 根据字体 ID 从 ResourceManage 查 FontName / FamilyName。 */
    private static String lookupFontName(OFDReader reader, int fontId) {
        try {
            org.ofdrw.reader.ResourceManage res = reader.getResMgt();
            if (res == null) return null;
            org.ofdrw.core.text.font.CT_Font font = res.getFont(String.valueOf(fontId));
            if (font == null) return null;
            String name = font.getFontName();
            if (name == null || name.isEmpty()) {
                name = font.getFamilyName();
            }
            return name;
        } catch (Throwable ignored) {}
        return null;
    }

    /** 解析图片对象，读取 Res 目录内嵌图片二进制，写入独立 FFI 内存，返回其指针。 */
    private static Pointer buildImage(ImageObject image, OFDReader reader, List<Pointer> allocs) {
        if (image == null) {
            return null;
        }
        long resourceId = 0L;
        if (image.getResourceID() != null && image.getResourceID().getRefId() != null
                && image.getResourceID().getRefId().getId() != null) {
            resourceId = image.getResourceID().getRefId().getId();
        }

        double width = 0.0, height = 0.0;
        if (image.getBoundary() != null) {
            if (image.getBoundary().getWidth() != null) {
                width = image.getBoundary().getWidth();
            }
            if (image.getBoundary().getHeight() != null) {
                height = image.getBoundary().getHeight();
            }
        }

        double[] ctm = ctmToArray(image);

        // 读取内嵌图片二进制字节
        byte[] imgBytes = readImageBytes(image, reader);
        Pointer dataPtr = writeBytes(imgBytes);
        allocs.add(dataPtr);

        Pointer mem = new Pointer(Native.malloc(OfdStructs.IMAGE_SIZE));
        mem.setLong(OfdStructs.IMAGE_RESID_OFF, resourceId);
        mem.setDouble(OfdStructs.IMAGE_WIDTH_OFF, width);
        mem.setDouble(OfdStructs.IMAGE_HEIGHT_OFF, height);
        for (int i = 0; i < OfdStructs.CTM_SIZE; i++) {
            mem.setDouble(OfdStructs.IMAGE_CTM_OFF + (long) i * 8, ctm[i]);
        }
        mem.setPointer(OfdStructs.IMAGE_DATA_OFF, dataPtr);
        mem.setInt(OfdStructs.IMAGE_DATASIZE_OFF, (imgBytes == null) ? 0 : imgBytes.length);
        allocs.add(mem);
        return mem;
    }

    /** 解析路径对象，写入独立 FFI 内存，返回其指针。 */
    private static Pointer buildPath(PathObject path, List<Pointer> allocs,
            java.util.Map<String, org.dom4j.Element> gradMap) {
        if (path == null) {
            return null;
        }
        String data = path.getAbbreviatedData();
        if (data == null) {
            data = "";
        }
        // DEBUG intro-数科.ofd path coords
        try {
            var b = path.getBoundary();
            double[] ctm = ctmToArray(path);
            System.err.printf("[Path.DEBUG] Boundary=[%.2f %.2f %.2f %.2f] CTM=[%.2f %.2f %.2f %.2f %.2f %.2f] dataLen=%d fillFlag=%s%n",
                b == null ? 0 : b.getTopLeftX(), b == null ? 0 : b.getTopLeftY(),
                b == null ? 0 : b.getWidth(), b == null ? 0 : b.getHeight(),
                ctm[0], ctm[1], ctm[2], ctm[3], ctm[4], ctm[5],
                data.length(), path.getFill());
        } catch (Throwable ignored) {}

        // ===== Stroke / Fill 布尔属性（最关键！）=====
        // XML 里显式写 Stroke="false" 时必须不描边（无论 StrokeColor 是什么）；
        // 显式写 Fill="false" 时必须不填充。
        // 未设置时才用颜色默认（Stroke 默认黑描边，Fill 默认透明）。
        Boolean strokeFlag = path.getStroke();
        Boolean fillFlag   = path.getFill();
        int strokeArgb, fillArgb;

        if (Boolean.FALSE.equals(strokeFlag)) {
            strokeArgb = 0x00000000;  // 强制不描边
        } else {
            strokeArgb = colorToArgb(path.getStrokeColor(), 0xFF000000);  // 默认黑色描边
        }
        if (Boolean.FALSE.equals(fillFlag)) {
            fillArgb = 0x00000000;   // 强制不填充
        } else {
            fillArgb = colorToArgb(path.getFillColor(), 0x00000000);     // 默认透明
        }
        // Path 渐变填充 —— 从 gradMap 拿原始 XML FillColor
        Pointer gradPtr = null;
        try {
            // ★ PathObject 和 TextObject 一样，XML 属性是 ID 不是 ObjID！
            String objId = null;
            try {
                java.lang.reflect.Method m = path.getClass().getMethod("getID");
                Object idObj = m.invoke(path);
                objId = (idObj != null) ? idObj.toString() : null;
            } catch (Throwable ignored) {
                try {
                    if (path.getObjID() != null) objId = path.getObjID().toString();
                } catch (Throwable ignored2) {}
            }
            if (objId != null && gradMap != null) {
                org.dom4j.Element fc = gradMap.get(objId);
                if (fc != null) {
                    double pBX = 0, pBY = 0, pBW = 1, pBH = 1;
                    if (path.getBoundary() != null) {
                        pBX = path.getBoundary().getTopLeftX();
                        pBY = path.getBoundary().getTopLeftY();
                        pBW = path.getBoundary().getWidth();
                        pBH = path.getBoundary().getHeight();
                    }
                    gradPtr = parseGradient(fc, allocs, pBX, pBY, pBW, pBH);
                    System.err.printf("[PATH_GRAD_HIT] PathObject ID=%s ptr=%s%n", objId, gradPtr);
                }
            }
        } catch (Throwable ignored) {}

        // LineWidth：OFD 默认 0（不描边），但我们给个小兜底避免 0 线宽 + Stroke=true 的情况下消失
        Double lw = path.getLineWidth();
        double strokeWidth = (lw == null || lw <= 0) ? 0.1 : lw;

        double[] ctm = ctmToArray(path);

        // ===== 其他图形属性 =====
        int strokeFlagInt = 0;   // 0=未设置 1=true 2=false
        int fillFlagInt   = 0;
        if (strokeFlag != null) strokeFlagInt = strokeFlag ? 1 : 2;
        if (fillFlag   != null) fillFlagInt   = fillFlag   ? 1 : 2;

        double miterLimit = 0.0;        // 0=未设置（OFD 默认 10）
        Double ml = path.getMiterLimit();
        if (ml != null) miterLimit = ml;

        int capType = 0;  // 0=Butt(OFD 默认) 1=Round 2=Square
        try {
            var cap = path.getCap();
            if (cap != null) {
                switch (cap.name()) {
                    case "Round":  capType = 1; break;
                    case "Square": capType = 2; break;
                    default:       capType = 0; break;
                }
            }
        } catch (Throwable ignored) {}

        int joinType = 0;  // 0=Miter(OFD 默认) 1=Round 2=Bevel
        try {
            var join = path.getJoin();
            if (join != null) {
                switch (join.name()) {
                    case "Round":  joinType = 1; break;
                    case "Bevel":  joinType = 2; break;
                    default:       joinType = 0; break;
                }
            }
        } catch (Throwable ignored) {}

        int alpha = 0;  // 0=未设置
        Integer a = path.getAlpha();
        if (a != null && a > 0 && a <= 255) alpha = a;

        Pointer dataPtr = writeString(data);
        allocs.add(dataPtr);

        Pointer mem = new Pointer(Native.malloc(OfdStructs.PATH_SIZE));
        mem.setPointer(OfdStructs.PATH_DATA_OFF, dataPtr);
        mem.setInt(OfdStructs.PATH_STROKE_OFF, strokeArgb);
        mem.setInt(OfdStructs.PATH_FILL_OFF, fillArgb);
        mem.setDouble(OfdStructs.PATH_WIDTH_OFF, strokeWidth);
        for (int i = 0; i < OfdStructs.CTM_SIZE; i++) {
            mem.setDouble(OfdStructs.PATH_CTM_OFF + (long) i * 8, ctm[i]);
        }
        mem.setInt(OfdStructs.PATH_STROKEFLAG_OFF, strokeFlagInt);
        mem.setInt(OfdStructs.PATH_FILLFLAG_OFF,   fillFlagInt);
        mem.setDouble(OfdStructs.PATH_MITERLIMIT_OFF, miterLimit);
        mem.setInt(OfdStructs.PATH_CAP_OFF,  capType);
        mem.setInt(OfdStructs.PATH_JOIN_OFF, joinType);
        mem.setInt(OfdStructs.PATH_ALPHA_OFF, alpha);
        mem.setPointer(OfdStructs.PATH_GRADIENT_OFF, gradPtr);
        allocs.add(mem);
        return mem;
    }

    /** 构建页面元素容器，写入独立 FFI 内存，登记释放，返回其指针。 */
    private static Pointer buildContainer(List<Pointer> textPtrs, List<Pointer> imagePtrs,
                                          List<Pointer> pathPtrs, List<Pointer> allocs) {
        int textCount = textPtrs.size();
        int imageCount = imagePtrs.size();
        int pathCount = pathPtrs.size();

        Pointer textsArr = buildPointerArray(textPtrs, allocs);
        Pointer imagesArr = buildPointerArray(imagePtrs, allocs);
        Pointer pathsArr = buildPointerArray(pathPtrs, allocs);

        Pointer containerMem = new Pointer(Native.malloc(OfdStructs.CONTAINER_SIZE));
        containerMem.setInt(OfdStructs.CONTAINER_TEXTCOUNT_OFF, textCount);
        containerMem.setPointer(OfdStructs.CONTAINER_TEXTS_OFF, textsArr);
        containerMem.setInt(OfdStructs.CONTAINER_IMGCOUNT_OFF, imageCount);
        containerMem.setPointer(OfdStructs.CONTAINER_IMAGES_OFF, imagesArr);
        containerMem.setInt(OfdStructs.CONTAINER_PATHCOUNT_OFF, pathCount);
        containerMem.setPointer(OfdStructs.CONTAINER_PATHS_OFF, pathsArr);
        allocs.add(containerMem);

        // 登记容器 -> 全部内存块
        ELEMENT_ALLOCATIONS.put(containerMem, allocs);
        return containerMem;
    }

    /** 将元素指针列表写入指针数组 FFI 内存（登记释放），返回其指针。 */
    private static Pointer buildPointerArray(List<Pointer> ptrs, List<Pointer> allocs) {
        if (ptrs == null || ptrs.isEmpty()) {
            return null;
        }
        Pointer arr = new Pointer(Native.malloc((long) ptrs.size() * Native.POINTER_SIZE));
        for (int i = 0; i < ptrs.size(); i++) {
            arr.setPointer((long) i * Native.POINTER_SIZE, ptrs.get(i));
        }
        allocs.add(arr);
        return arr;
    }

    /** 将字符串编码为 UTF-8 带 NUL 的 FFI 内存。 */
    private static Pointer writeString(String s) {
        byte[] bytes = (s == null) ? new byte[0] : s.getBytes(StandardCharsets.UTF_8);
        return writeBytes(bytes);
    }

    /** 将字节数组写入带结尾 NUL 的 FFI 内存。 */
    private static Pointer writeBytes(byte[] bytes) {
        if (bytes == null) {
            bytes = new byte[0];
        }
        byte[] out = new byte[bytes.length + 1];
        System.arraycopy(bytes, 0, out, 0, bytes.length);
        out[bytes.length] = 0;
        Pointer p = new Pointer(Native.malloc(out.length));
        p.write(0, out, 0, out.length);
        return p;
    }

    /* ==================== 属性提取 ==================== */

    /** 读取图形单元（文本/图片/路径）的 CTM 变换矩阵（6 个浮点）。 */
    private static double[] ctmToArray(CT_GraphicUnit<?> gu) {
        // 默认单位矩阵（恒等变换），避免 ofdrw 在 null CTM 时返回全零导致渲染坍缩
        double[] out = { 1.0, 0.0, 0.0, 1.0, 0.0, 0.0 };
        double[] raw = { 1.0, 0.0, 0.0, 1.0, 0.0, 0.0 };
        if (gu == null || gu.getCTM() == null) {
            // null CTM → identity
        } else {
            Double[] d = gu.getCTM().toDouble();
            if (d != null) {
                for (int i = 0; i < Math.min(d.length, OfdStructs.CTM_SIZE); i++) {
                    out[i] = (d[i] == null) ? 0.0 : d[i];
                    raw[i] = out[i];
                }
            }
        }
        // 将 Boundary 左上角位移合入 CTM 的 translate 分量 (e,f)
        // OFD 总体变换 = translate(Boundary.x, Boundary.y) × CTM
        // => CTM.e += Boundary.x,  CTM.f += Boundary.y
        Double bx = null, by = null;
        try {
            if (gu.getBoundary() != null) {
                bx = gu.getBoundary().getTopLeftX();
                by = gu.getBoundary().getTopLeftY();
                if (bx != null) out[4] += bx;
                if (by != null) out[5] += by;
            }
        } catch (Throwable ignored) {}
        // DEBUG: 公章专用日志（非 identity CTM → 印章弯字）
        if (Math.abs(raw[0]) < 0.99 || Math.abs(raw[1]) > 0.01 || Math.abs(raw[2]) > 0.01 || Math.abs(raw[3]) < 0.99) {
            System.err.printf("[CTM] raw=[%.2f %.2f %.2f %.2f %.2f %.2f] boundary=(%s,%s) merged=[%.2f %.2f %.2f %.2f %.2f %.2f]%n",
                raw[0], raw[1], raw[2], raw[3], raw[4], raw[5],
                bx != null ? String.format("%.2f", bx) : "null",
                by != null ? String.format("%.2f", by) : "null",
                out[0], out[1], out[2], out[3], out[4], out[5]);
        }
        return out;
    }

    /** 读取图片原始二进制（优先取原始资源字节，失败则从解码图像回写）。 */
    private static byte[] readImageBytes(ImageObject image, OFDReader reader) {
        try {
            ResourceManage res = reader.getResMgt();
            if (res == null) {
                System.err.println("[DEBUG] readImageBytes: resMgt is null");
                return new byte[0];
            }
            String refId = resourceIdString(image);
            System.err.println("[DEBUG] readImageBytes: refId=" + refId);
            BufferedImage bmp = res.getImage(image);
            if (bmp == null) {
                System.err.println("[DEBUG] readImageBytes: bmp is null");
                return new byte[0];
            }
            ByteArrayOutputStream bos = new ByteArrayOutputStream();
            boolean ok = ImageIO.write(bmp, "png", bos);
            System.err.println("[DEBUG] readImageBytes: ok=" + ok + " size=" + bmp.getWidth() + "x" + bmp.getHeight() + " pngBytes=" + bos.size());
            return bos.toByteArray();
        } catch (Throwable t) {
            System.err.println("[DEBUG] readImageBytes EXCEPTION: " + t);
            t.printStackTrace();
            return new byte[0];
        }
    }

    /** 提取图片资源引用 ID 字符串。 */
    private static String resourceIdString(ImageObject image) {
        try {
            if (image.getResourceID() != null && image.getResourceID().getRefId() != null
                    && image.getResourceID().getRefId().getId() != null) {
                return String.valueOf(image.getResourceID().getRefId().getId());
            }
        } catch (Throwable ignored) {
            // 忽略提取异常
        }
        return null;
    }

    /** 将 OFD 颜色（CT_Color）转换为 ARGB int。
     *  @param color OFD 颜色对象
     *  @param defaultArgb color 为 null 时返回的默认值。
     *                     Text 的 FillColor 默认 0xFF000000（黑色）；
     *                     Path 的 FillColor/StrokeColor 默认 0x00000000（透明/无）。
     */
    private static int colorToArgb(CT_Color color, int defaultArgb) {
        if (color == null) {
            return defaultArgb;
        }
        int alpha = 255;
        try {
            if (color.getAlpha() != null) {
                alpha = color.getAlpha();
            }
        } catch (Throwable ignored) {
        }
        int r = 0, g = 0, b = 0;
        try {
            if (color.getValue() != null) {
                Double[] c = color.getValue().toDouble();
                if (c != null && c.length > 0) {
                    double r0 = (c[0] == null) ? 0.0 : c[0];
                    double g0 = (c.length > 1 && c[1] != null) ? c[1] : r0;
                    double b0 = (c.length > 2 && c[2] != null) ? c[2] : r0;
                    r = clamp(r0);
                    g = clamp(g0);
                    b = clamp(b0);
                }
            } else {
                // ★ 渐变（AxialShd / RadialShd）：手动拿第一个 Segment Color
                try {
                    // CT_Color 继承 DefaultElementProxy implements dom4j.Element → 直接 cast！
                    org.dom4j.Element el = (org.dom4j.Element) color;
                    org.dom4j.Node shd = el.element("AxialShd");
                    if (shd == null) shd = el.element("RadialShd");
                    if (shd != null) {
                        org.dom4j.Element shdEl = (org.dom4j.Element) shd;
                        org.dom4j.Node firstSeg = shdEl.element("Segment");
                        if (firstSeg != null) {
                            org.dom4j.Node colorNode = ((org.dom4j.Element) firstSeg).element("Color");
                            if (colorNode != null) {
                                String val = ((org.dom4j.Element) colorNode).attributeValue("Value");
                                if (val != null && !val.isEmpty()) {
                                    String[] parts = val.trim().split("\\s+");
                                    if (parts.length >= 3) {
                                        r = clamp(Double.parseDouble(parts[0]));
                                        g = clamp(Double.parseDouble(parts[1]));
                                        b = clamp(Double.parseDouble(parts[2]));
                                        System.err.printf("[Gradient] %s fallback → color=(%d,%d,%d)%n",
                                            shdEl.getName(), r, g, b);
                                    }
                                }
                            }
                        }
                    }
                } catch (Throwable t) {
                    System.err.printf("[Gradient] fallback error: %s%n", t.getMessage());
                }
            }
        } catch (Throwable ignored) {
        }
        return ((alpha & 0xFF) << 24) | ((r & 0xFF) << 16) | ((g & 0xFF) << 8) | (b & 0xFF);
    }

    /**
     * 从 CT_Color（dom4j Element）里解析渐变，如果有 AxialShd/RadialShd 则 malloc
     * 一块内存存 OfdGradient + stops[]，返回 Pointer。无渐变返回 null。
     *
     * 内存布局（pack=8）：
     *   header (48B): type(4+4pad), startX(8), startY(8), endX(8), endY(8), segCount(4+4pad)
     *   stops[segCount] (每个 16B): position(8), argb(4+4pad)
     */
    private static Pointer parseGradient(org.dom4j.Element colorEl, List<Pointer> allocs,
                                          double bX, double bY, double bW, double bH) {
        if (colorEl == null) return null;
        org.dom4j.Element shd = colorEl.element("AxialShd");
        int type = 0;
        if (shd != null) {
            type = 1; // Axial
        } else {
            shd = colorEl.element("RadialShd");
            if (shd != null) type = 2; // Radial
        }
        if (shd == null) return null;

        // 解析 StartPoint / EndPoint
        String startStr = shd.attributeValue("StartPoint");
        String endStr   = shd.attributeValue("EndPoint");
        String centerStr = shd.attributeValue("CenterPoint");

        double startX = 0, startY = 0, endX = 0, endY = 0;
        if (startStr != null) {
            String[] p = startStr.trim().split("\\s+");
            if (p.length >= 2) { startX = Double.parseDouble(p[0]); startY = Double.parseDouble(p[1]); }
        }
        if (endStr != null) {
            String[] p = endStr.trim().split("\\s+");
            if (p.length >= 2) { endX = Double.parseDouble(p[0]); endY = Double.parseDouble(p[1]); }
        }
        if (centerStr != null) {
            String[] p = centerStr.trim().split("\\s+");
            if (p.length >= 2) { startX = Double.parseDouble(p[0]); startY = Double.parseDouble(p[1]); }
        }

        // ★ 关键修复：存 OFD 原始 StartPoint/EndPoint 的页面级 mm 绝对坐标！
        // 这样同一行的多个 TextObject（如 "2013" + "年巴塞罗那..."）
        // 共享同一个页面级渐变空间，渐变才能跨字连续！
        // C++ 侧用 LogicalMode，直接用这些 mm 坐标作为渐变坐标。
        double dx = endX - startX;
        double dy = endY - startY;
        if (type == 1) {
            // AxialShd：保持原始 OFD 页面级坐标不变！
            // 但如果渐变方向几乎垂直（|dy| >> |dx|），保持原坐标也可以
            // 我们现在什么都不做——直接把原始坐标传给 C++
        } else {
            // RadialShd：center 原始坐标也是页面级的，end 默认 0.5（C++ 会用 endX/endY 作为 radius 中心）
        }

        // 解析 Segments
        java.util.List<Double> positions = new java.util.ArrayList<>();
        java.util.List<Integer> argbs    = new java.util.ArrayList<>();
        for (Object segObj : shd.elements("Segment")) {
            org.dom4j.Element seg = (org.dom4j.Element) segObj;
            String posStr = seg.attributeValue("Position");
            double pos = (posStr != null) ? Double.parseDouble(posStr) : 0;
            // 确保在 0..1 范围
            if (pos < 0) pos = 0;
            if (pos > 1) pos = 1;
            org.dom4j.Node colorNode = seg.element("Color");
            if (colorNode == null) continue;
            String val = ((org.dom4j.Element) colorNode).attributeValue("Value");
            if (val == null) continue;
            String[] parts = val.trim().split("\\s+");
            if (parts.length < 3) continue;
            int r = clamp(Double.parseDouble(parts[0]));
            int g = clamp(Double.parseDouble(parts[1]));
            int b = clamp(Double.parseDouble(parts[2]));
            int a = 255;
            int argb = (a << 24) | (r << 16) | (g << 8) | b;
            positions.add(pos);
            argbs.add(argb);
        }
        if (positions.isEmpty()) return null;

        // 排序：按 position 升序（OFD 规范要求，但有些生成器乱序）
        // 简单做法：用两重循环排
        for (int i = 0; i < positions.size(); i++) {
            for (int j = i + 1; j < positions.size(); j++) {
                if (positions.get(i) > positions.get(j)) {
                    double tp = positions.get(i); positions.set(i, positions.get(j)); positions.set(j, tp);
                    int tc = argbs.get(i); argbs.set(i, argbs.get(j)); argbs.set(j, tc);
                }
            }
        }

        // 分配内存：header 48B + stops * 16B
        int segCount = positions.size();
        int totalSize = 48 + segCount * 16;
        Pointer mem = new Pointer(Native.malloc(totalSize));
        if (mem == null) return null;
        allocs.add(mem);
        System.err.printf("[Gradient] type=%d start=(%.2f,%.2f) end=(%.2f,%.2f) stops=%d ptr=%s%n",
            type, startX, startY, endX, endY, segCount, mem);

        // 写 header
        mem.setInt(0, type);
        mem.setDouble(8,  startX);
        mem.setDouble(16, startY);
        mem.setDouble(24, endX);
        mem.setDouble(32, endY);
        mem.setInt(40, segCount);

        // 写 stops
        long stopBase = 48L;
        for (int i = 0; i < segCount; i++) {
            long off = stopBase + i * 16L;
            mem.setDouble(off, positions.get(i));
            mem.setInt(off + 8, argbs.get(i));
        }
        return mem;
    }

    /** 将颜色分量收敛到 0-255。 */
    private static int clamp(double v) {
        int i = (int) Math.round(v);
        return Math.max(0, Math.min(255, i));
    }
}