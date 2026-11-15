# 重新生成 PDF 转 Markdown 测试用的 PDF。
#
# 依赖 PyMuPDF (pip install pymupdf)：
#   python tests/data/generate_fixtures.py
#
# 生成：
#   sample-rich.pdf       标题 / 段落 / 粗斜体 / 列表 / 图片 / 重复页眉页脚
#   sample-encrypted.pdf  用 user 密码 "mapdfr" 加密的 sample.pdf

import os

import pymupdf

HERE = os.path.dirname(os.path.abspath(__file__))
RICH = os.path.join(HERE, "sample-rich.pdf")
ENCRYPTED = os.path.join(HERE, "sample-encrypted.pdf")
PLAIN = os.path.join(HERE, "sample.pdf")
IMAGE = os.path.join(HERE, "_rich_img.png")

pix = pymupdf.Pixmap(pymupdf.csRGB, pymupdf.IRect(0, 0, 160, 100), False)
pix.set_rect(pix.irect, (210, 80, 60))
pix.save(IMAGE)

doc = pymupdf.open()

for page_no in (1, 2):
    page = doc.new_page(width=595, height=842)
    # 重复的页眉 / 页脚（页码不同，归一化后相同）。
    page.insert_text((72, 40), "MaPdfR 示例文档", fontsize=8, fontname="helv")
    page.insert_text((72, 800), f"- {page_no} -", fontsize=8, fontname="helv")

    if page_no == 1:
        html = """
        <h1 style="font-size:24pt;margin:0 0 8px 0">MaPdfR 转换测试</h1>
        <h2 style="font-size:16pt;margin:0 0 6px 0">1. 简介</h2>
        <p style="font-size:11pt">
          这是一段<b>加粗</b>、<i>斜体</i>以及普通文字混排的段落，
          用来验证 PDF 转 Markdown 的段落合并与行内样式。
        </p>
        <h2 style="font-size:16pt;margin:10px 0 6px 0">2. 列表</h2>
        <ul style="font-size:11pt">
          <li>第一项：提取文字</li>
          <li>第二项：保留图片</li>
        </ul>
        <p style="font-size:11pt">
          Plain <b>bold</b> and <i>italic</i> words test inline styles.
        </p>
        <h3 style="font-size:13pt;margin:10px 0 4px 0">2.1 英文段落</h3>
        <p style="font-size:11pt">
          The quick brown fox jumps over the lazy dog. This sentence is
          long enough to wrap across several lines so that paragraph
          merging can be exercised.
        </p>
        """
    else:
        html = """
        <h2 style="font-size:16pt;margin:0 0 6px 0">3. 第二页</h2>
        <p style="font-size:11pt">
          第二页用来验证跨页处理，以及重复页眉页脚是否会被去掉。
          页面底部和顶部的固定文字不应出现在 Markdown 中。
        </p>
        """

    page.insert_htmlbox(pymupdf.Rect(72, 70, 525, 700), html)

    if page_no == 1:
        page.insert_image(pymupdf.Rect(72, 660, 232, 760), filename=IMAGE)

doc.subset_fonts()
doc.save(RICH, garbage=4, deflate=True)
os.remove(IMAGE)

plain = pymupdf.open(PLAIN)
plain.save(
    ENCRYPTED,
    encryption=pymupdf.PDF_ENCRYPT_AES_256,
    user_pw="mapdfr",
    owner_pw="owner",
)

print("wrote", RICH)
print("wrote", ENCRYPTED)

# 阅读器导航、打印和批注测试。第二页横排并旋转，便于发现页面坐标问题。
nav = pymupdf.open()
for page_no, size in enumerate(((595, 842), (720, 400), (430, 610)), start=1):
    page = nav.new_page(width=size[0], height=size[1])
    page.insert_text((50, 90), f"Inspection checkpoint {page_no}", fontsize=20)
    page.draw_rect(pymupdf.Rect(50, 120, 170, 185), color=(0, 0, 0), fill=(0.1, 0.3, 0.7))
    if page_no == 2:
        page.set_rotation(90)
nav.set_toc([[1, "Introduction", 1], [1, "Inspection", 2], [2, "Appendix", 3]])
nav.set_page_labels([
    {"startpage": 0, "prefix": "QC-", "style": "D", "firstpagenum": 1},
])
nav.set_metadata({"title": "MaPdfR QA", "author": "Document Control"})
nav.save(os.path.join(HERE, "sample-navigation.pdf"))

for name, permissions in (
    ("sample-restricted.pdf", 0),
    ("sample-review-encrypted.pdf", pymupdf.PDF_PERM_ANNOTATE | pymupdf.PDF_PERM_PRINT),
):
    nav.save(
        os.path.join(HERE, name),
        encryption=pymupdf.PDF_ENCRYPT_AES_256,
        user_pw="review",
        owner_pw="qa-owner",
        permissions=permissions,
    )

signature = pymupdf.Widget()
signature.field_type = pymupdf.PDF_WIDGET_TYPE_SIGNATURE
signature.field_name = "Approval"
signature.rect = pymupdf.Rect(200, 120, 450, 185)
nav[0].add_widget(signature)
nav.save(os.path.join(HERE, "sample-signature-field.pdf"))
