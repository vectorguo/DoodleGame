#!/usr/bin/env python3
"""生成 Doodle 的应用图标，并铺进 Android 的资源目录。

    python3 android/icon/make_icon.py

为什么是脚本而不是一张画好的图：图标要按 5 种密度、4 种用途（旧版方形、
旧版圆形、自适应前景、单色）各出一份，共二十来个文件。手改一次就是二十次
改动，而且改不齐。这里所有几何与配色只写一份，跑一遍全部重算。

图形是「随手画的一个螺旋」—— d/o/o/d/l/e 这几个字母里的 o，一笔画到底。
没有文字：图标在桌面上最小只有 48px，字到那个尺寸就糊成一团了。

产出落在 android/app/src/main/res/ 下：
    values/ic_launcher_background.xml          自适应图标的底色
    mipmap-anydpi-v26/ic_launcher{,_round}.xml 自适应图标（API 26+）
    mipmap-*dpi/ic_launcher{,_round}.png       旧版图标（API 24、25 用）
    mipmap-*dpi/ic_launcher_foreground.png     自适应图标的前景层
"""

import math
import os

from PIL import Image, ImageDraw

# ---------- 配色 ----------

BG = (255, 201, 60)     # 底：暖黄，像一张便签纸
INK = (31, 42, 68)      # 笔：墨蓝，比纯黑温和，和暖黄对比也够

# ---------- 画布与安全区 ----------

STROKE = 0.105  # 线宽，相对图形直径

# 自适应图标是 108dp 见方，但 launcher 只露出中间 72dp，还会按自己的形状去裁
# （圆的、方的、超椭圆的都有）。唯一保证裁不到的是正中间那个直径 66dp 的圆。
#
# 这里取 62dp 而不是顶满 66dp：顶满时最外圈会贴着圆形遮罩的边缘，看着发闷，
# 留 4dp 反而更稳。DP_MARK 是「图形外轮廓（含线宽）」的直径，
# 曲线本身归一化到的直径要再减去一个线宽
DP_CANVAS = 108
DP_VISIBLE = 72
DP_SAFE = 66
DP_MARK = 62
SAFE_ART = (DP_MARK / DP_CANVAS) / (1 + STROKE)   # ≈ 0.520

# 旧版图标没有安全区这一说，系统原样贴出来（API 25 的 launcher 会自己裁圆）。
# 没有 108dp 画布那圈余量，留白按惯例放到 25%，比自适应那份松
LEGACY_ART = 0.68
# 旧版圆形图标：四角要被切掉，图形再收一点
LEGACY_ROUND_ART = 0.60

DENSITIES = [("mdpi", 1), ("hdpi", 1.5), ("xhdpi", 2), ("xxhdpi", 3), ("xxxhdpi", 4)]
LEGACY_DP = 48
FOREGROUND_DP = 108

SS = 4  # 超采样倍数：PIL 的 draw 不带抗锯齿，先画大再缩回来


# ---------- 图形 ----------

def spiral_pts(turns=2.6, r0=0.13, grow=1.0, wob=0.035, ell=0.06, n=8000):
    """一笔螺旋。返回归一化坐标（最大半径 ≈ 1）。

    turns / r0 / grow 决定疏密，三者的组合是拿几十个变体试出来的：
    圈数再多、或者 grow 偏离 1 太多，环与环的间隙就会小于线宽，缩到 48px
    时整团糊成一个点。

    wob 是手绘感：半径上叠两个低频正弦，让环距不完全均匀。这个是「像人画的」
    和「像机器画的」之间唯一的区别，但只能给到这个量 —— 到 0.07 外圈就开始
    起疙瘩，小尺寸下比规整还难看。

    ell 把圆压扁一点点（6%），去掉正圆那种机械感。
    """
    end = 2 * math.pi * turns
    pts = []
    for i in range(n + 1):
        t = i / n
        th = end * t
        r = r0 + (1.0 - r0) * (t ** grow)
        r *= 1 + wob * math.sin(2.7 * th + 0.6) + 0.4 * wob * math.sin(5.9 * th + 2.1)
        pts.append((r * math.cos(th) * (1 + ell), r * math.sin(th) * (1 - ell)))
    return pts


def render_mark(size, art_frac, stroke_frac=STROKE, ink=INK):
    """把螺旋画成一张 size×size 的 RGBA 图，图形占画布 art_frac 的比例。

    关键细节：整张图的 RGB 恒等于笔色，只有 alpha 在变。降采样时 PIL 是四个
    通道各缩各的，如果透明区域的 RGB 是 0，边缘就会把黑色混进来，描边外圈会
    出现一圈脏灰。先把底色铺成「同样 RGB 但 alpha=0」，缩完 RGB 还是同一个值，
    边缘就只剩 alpha 的过渡了。
    """
    c = size * SS
    img = Image.new("RGBA", (c, c), ink + (0,))
    draw = ImageDraw.Draw(img)

    pts = spiral_pts()
    # 按最大半径归一化（不是按包围盒）：保证图形始终是正圆居中的，
    # 不会因为某一圈甩得远就把整体推偏
    maxr = max(math.hypot(x, y) for x, y in pts)
    r_out = c * art_frac / 2.0
    s = r_out / maxr
    p = [(c / 2 + x * s, c / 2 + y * s) for x, y in pts]

    w = stroke_frac * (c * art_frac)
    half = w / 2.0
    # 线段 + 每个采样点上盖一个圆：这样拐角是圆的，收笔也是圆的（圆头笔触）
    for (x0, y0), (x1, y1) in zip(p, p[1:]):
        draw.line([x0, y0, x1, y1], fill=ink + (255,), width=max(1, round(w)))
    for x, y in p:
        draw.ellipse([x - half, y - half, x + half, y + half], fill=ink + (255,))

    return img.resize((size, size), Image.LANCZOS)


def on_bg(mark, size, bg=BG):
    return Image.alpha_composite(Image.new("RGBA", (size, size), bg + (255,)), mark)


def circle_mask(size, ss=4):
    """圆形蒙版，用来预览旧版圆形图标会被裁成什么样"""
    m = Image.new("L", (size * ss, size * ss), 0)
    ImageDraw.Draw(m).ellipse([0, 0, size * ss - 1, size * ss - 1], fill=255)
    return m.resize((size, size), Image.LANCZOS)


# ---------- 落盘 ----------

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))  # android/
RES = os.path.join(ROOT, "app", "src", "main", "res")

ADAPTIVE_XML = """<?xml version="1.0" encoding="utf-8"?>
<!-- 自适应图标。API 26 起系统只读这个文件，下面的 mipmap-*dpi/*.png 是给
     API 24、25 用的旧版图，两套并存不是冗余

     背景用颜色资源（一个纯色），前景是图形本身。这是最省的做法：
     背景层不占字节，前景层只有一张位图 -->
<adaptive-icon xmlns:android="http://schemas.android.com/apk/res/android">
    <background android:drawable="@color/ic_launcher_background" />
    <foreground android:drawable="@mipmap/ic_launcher_foreground" />
    <!-- Android 13 起的主题化图标：系统会拿这一层按用户选的壁纸配色重新着色。
         用的就是前景那张图，它的 RGB 处处相同、只有 alpha 在变，系统换色时
         不会留下原来的墨蓝 -->
    <monochrome android:drawable="@mipmap/ic_launcher_foreground" />
</adaptive-icon>
"""

BG_XML = """<?xml version="1.0" encoding="utf-8"?>
<!-- 自适应图标的底色。改这里就够了，前景的图形是透明的，不参与 -->

<resources>
    <color name="ic_launcher_background">#{color}</color>
</resources>
"""


def write(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        f.write(text)
    rel = os.path.relpath(path, ROOT)
    print(f"  {rel}")


# 前景图的 alpha 分档数。
#
# 抗锯齿让描边边缘有 256 级 alpha，zlib 对这种连续渐变几乎压不动，是整个图标
# 里最大的一块。分到 32 档后体积掉到六分之一，而叠回底色上看到的色差最多 7
# （满值 255），再往上加档也只是把平均差从 0.07 降到 0.05，不值得
ALPHA_LEVELS = 32


def save_png(img, path, single_ink):
    """落盘。single_ink=False 时走通用调色板量化。

    single_ink=True（前景层）时不用通用量化，而是自己搭一个「调色板里全是同一个
    笔色、只有 alpha 不同」的 P 模式图。这样做的两个理由：

    1. 体积 —— P 模式每像素 1 字节，同样内容比 RGBA 小
    2. 保住 render_mark 那条「RGB 处处相同」的性质。通用量化（FASTOCTREE 之类）
       会在调色板里混进 (0,0,255)、(85,0,85) 这类谁也不认识的颜色 —— 它们只被
       最外圈几个 alpha 近乎 0 的像素引用，叠在黄底上确实看不出来，但哪天换了
       底色就会显形。自己搭调色板就没有这个隐患，而且更小

    旧版那两张底色和图形是两种颜色，anti-alias 出来的是两色的混合，没有
    「单一 RGB」可言，所以走通用量化。
    """
    if single_ink:
        step = 255 / (ALPHA_LEVELS - 1)
        idx = img.getchannel("A").point(
            [min(ALPHA_LEVELS - 1, round(v / step)) for v in range(256)]
        )
        out = Image.new("P", img.size)
        out.putpalette(list(INK) * ALPHA_LEVELS)
        out.putdata(list(idx.getdata()))
        out.save(
            path,
            optimize=True,
            transparency=bytes(min(255, round(i * step)) for i in range(ALPHA_LEVELS)),
        )
    else:
        img.quantize(colors=64, method=Image.FASTOCTREE).save(path, optimize=True)
    return os.path.getsize(path)


def main():
    print("生成图标：")
    total = 0

    # --- 自适应图标（API 26+）：前景透明，底色交给颜色资源 ---
    for name, scale in DENSITIES:
        size = round(FOREGROUND_DP * scale)
        d = os.path.join(RES, f"mipmap-{name}")
        os.makedirs(d, exist_ok=True)
        n = save_png(
            render_mark(size, SAFE_ART),
            os.path.join(d, "ic_launcher_foreground.png"),
            single_ink=True,
        )
        total += n
        print(f"  app/src/main/res/mipmap-{name}/ic_launcher_foreground.png  {size}px")

    for which in ("ic_launcher", "ic_launcher_round"):
        write(os.path.join(RES, "mipmap-anydpi-v26", f"{which}.xml"), ADAPTIVE_XML)

    write(
        os.path.join(RES, "values", "ic_launcher_background.xml"),
        BG_XML.format(color="%02X%02X%02X" % BG),
    )

    # --- 旧版图标（API 24、25）：底色烤进位图，因为那两版没有背景层这回事 ---
    for name, scale in DENSITIES:
        size = round(LEGACY_DP * scale)
        d = os.path.join(RES, f"mipmap-{name}")
        os.makedirs(d, exist_ok=True)

        total += save_png(
            on_bg(render_mark(size, LEGACY_ART), size),
            os.path.join(d, "ic_launcher.png"),
            single_ink=False,
        )

        rd = on_bg(render_mark(size, LEGACY_ROUND_ART), size)
        rd.putalpha(circle_mask(size))
        total += save_png(rd, os.path.join(d, "ic_launcher_round.png"), single_ink=False)

        print(f"  app/src/main/res/mipmap-{name}/ic_launcher{{,_round}}.png  {size}px")

    # --- 预览图：不用装到机器上就能看效果 ---
    #
    # 这里要还原 launcher 的真实动作，不能直接把整块 108dp 画布拿去盖蒙版：
    # 系统只取中间 72dp（外圈各 18dp 是给视差留的余量，会被裁掉），把蒙版套在
    # 那 72dp 上，再把结果放大到桌面图标的大小。照整块画布盖蒙版会让人误以为
    # 图形只占圆的六成，实际是九成 —— 两者观感差很多
    cell = 192
    crop = int(cell * DP_VISIBLE / DP_CANVAS)

    def adaptive(mask):
        full = on_bg(render_mark(cell, SAFE_ART), cell)
        off = (cell - crop) // 2
        v = full.crop((off, off, off + crop, off + crop)).resize((cell, cell), Image.LANCZOS)
        v.putalpha(mask(cell))
        return v

    def circle(sz):
        return circle_mask(sz)

    def squircle(sz):
        ss = 4
        m = Image.new("L", (sz * ss, sz * ss), 0)
        ImageDraw.Draw(m).rounded_rectangle(
            [0, 0, sz * ss - 1, sz * ss - 1], radius=int(sz * ss * 0.30), fill=255
        )
        return m.resize((sz, sz), Image.LANCZOS)

    sheet = Image.new("RGBA", (cell * 4, cell), (238, 238, 238, 255))
    for i, m in enumerate([circle, squircle]):
        v = adaptive(m)
        sheet.paste(v, (cell * i, 0), v)

    # 旧版方形 / 旧版圆形（这两份是整块画布，不裁）
    sheet.paste(on_bg(render_mark(cell, LEGACY_ART), cell), (cell * 2, 0))
    r = on_bg(render_mark(cell, LEGACY_ROUND_ART), cell)
    r.putalpha(circle_mask(cell))
    sheet.paste(r, (cell * 3, 0), r)

    prev = os.path.join(ROOT, "icon", "preview.png")
    sheet.save(prev)
    print("  android/icon/preview.png  （自适应·圆 / 自适应·方 / 旧版·方 / 旧版·圆）")
    print(f"  位图合计 {total / 1024:.0f} KB")


if __name__ == "__main__":
    main()
