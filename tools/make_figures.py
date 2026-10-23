"""Regenerate docs/images/fps.svg and pipeline.svg for the README: python tools/make_figures.py docs/images"""
import os
import sys

OUT = sys.argv[1]

STYLE = """<style>
  .bg{fill:#fcfcfb} .ink{fill:#0b0b0b} .ink2{fill:#52514e} .mut{fill:#898781}
  .grid{stroke:#e1e0d9} .base{stroke:#c3c2b7} .s1{fill:#2a78d6} .s0{fill:#b9b7b0}
  .box{fill:#ffffff;stroke:#c3c2b7} .accent{fill:#e8f1fc;stroke:#2a78d6} .line{stroke:#52514e}
  .aline{stroke:#2a78d6} .tline{stroke:#898781} .ahead{fill:#2a78d6} .thead{fill:#898781}
  text{font-family:-apple-system,"Segoe UI",Roboto,Helvetica,Arial,sans-serif}
  @media (prefers-color-scheme: dark){
    .bg{fill:#1a1a19} .ink{fill:#f0efec} .ink2{fill:#c3c2b7}
    .grid{stroke:#2c2c2a} .base{stroke:#383835} .s1{fill:#3987e5} .s0{fill:#5c5b57}
    .box{fill:#232322;stroke:#4a4a47} .accent{fill:#1d2a3a;stroke:#3987e5} .line{stroke:#c3c2b7}
    .aline{stroke:#3987e5} .ahead{fill:#3987e5}
  }
</style>"""


def bar(x0, y, w, h, cls, r=4):
    """Horizontal bar: square at the baseline (x0), 4px rounded data end."""
    if w <= r:
        return f'<rect class="{cls}" x="{x0}" y="{y}" width="{w}" height="{h}"/>'
    x1 = x0 + w
    return (f'<path class="{cls}" d="M{x0},{y} H{x1 - r} Q{x1},{y} {x1},{y + r} V{y + h - r} '
            f'Q{x1},{y + h} {x1 - r},{y + h} H{x0} Z"/>')


def fps_svg():
    data = [  # label, sub-label, v1, v2
        ("Solid colour", "full screen", 17.0, 30.4),
        ("Scrolling UI", "full screen", 17.0, 30.4),
        ("Gradient", "full screen", 10.1, 16.8),
        ("Noise", "incompressible", 5.0, 5.0),
        ("40×40 square", "partial update", 92.1, 89.5),
    ]
    W, left, right = 760, 170, 70
    plot_w = W - left - right
    top, bh, gap, row = 70, 14, 2, 52
    H = top + row * len(data) + 40
    xmax = 100
    sx = lambda v: left + v / xmax * plot_w
    o = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}" '
         f'role="img" aria-label="Frames per second, v1 vs v2">', STYLE,
         f'<rect class="bg" width="{W}" height="{H}" rx="8"/>',
         '<text class="ink" x="20" y="30" font-size="16" font-weight="600">Frames per second drawn by the display</text>',
         # legend
         f'<rect class="s0" x="{left}" y="44" width="12" height="12" rx="2"/>',
         f'<text class="ink2" x="{left + 18}" y="54" font-size="12">v1: sequential</text>',
         f'<rect class="s1" x="{left + 130}" y="44" width="12" height="12" rx="2"/>',
         f'<text class="ink2" x="{left + 148}" y="54" font-size="12">v2: pipelined (current)</text>']
    bottom = top + row * len(data)
    for t in (0, 25, 50, 75, 100):
        x = sx(t)
        o.append(f'<line class="grid" x1="{x}" y1="{top - 6}" x2="{x}" y2="{bottom}" stroke-width="1"/>')
        o.append(f'<text class="mut" x="{x}" y="{bottom + 18}" font-size="11" text-anchor="middle">{t}</text>')
    o.append(f'<text class="mut" x="{left + plot_w / 2}" y="{bottom + 34}" font-size="11" text-anchor="middle">fps</text>')
    for i, (lab, sub, v1, v2) in enumerate(data):
        y = top + i * row + (row - 2 * bh - gap) / 2 - 4
        o.append(f'<text class="ink" x="{left - 12}" y="{y + 12}" font-size="13" text-anchor="end">{lab}</text>')
        o.append(f'<text class="mut" x="{left - 12}" y="{y + 27}" font-size="11" text-anchor="end">{sub}</text>')
        for j, (v, cls) in enumerate(((v1, "s0"), (v2, "s1"))):
            by = y + j * (bh + gap)
            w = sx(v) - left
            o.append(bar(left, by, w, bh, cls))
            weight = ' font-weight="600"' if cls == "s1" else ""
            ink = "ink" if cls == "s1" else "ink2"
            o.append(f'<text class="{ink}" x="{left + w + 6}" y="{by + 11}" font-size="11"{weight}>{v:g}</text>')
    o.append(f'<line class="base" x1="{left}" y1="{top - 6}" x2="{left}" y2="{bottom}" stroke-width="1"/>')
    o.append('</svg>')
    return "\n".join(o)


def pipeline_svg():
    W, H = 900, 330
    o = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}" '
         'role="img" aria-label="Data path from the Linux host to the panel">', STYLE,
         '<defs>'
         '<marker id="a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse">'
         '<path class="ahead" d="M0,0 L10,5 L0,10 Z"/></marker>'
         '<marker id="t" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse">'
         '<path class="thead" d="M0,0 L10,5 L0,10 Z"/></marker>'
         '</defs>',
         f'<rect class="bg" width="{W}" height="{H}" rx="8"/>']

    def box(x, y, w, h, title, lines=(), cls="box", size=13):
        o.append(f'<rect class="{cls}" x="{x}" y="{y}" width="{w}" height="{h}" rx="6" stroke-width="1.2"/>')
        cy = y + 22 if lines else y + h / 2 + 5
        o.append(f'<text class="ink" x="{x + w / 2}" y="{cy}" font-size="{size}" font-weight="600" text-anchor="middle">{title}</text>')
        for k, ln in enumerate(lines):
            o.append(f'<text class="ink2" x="{x + w / 2}" y="{cy + 18 + k * 15}" font-size="11" text-anchor="middle">{ln}</text>')

    def arrow(x1, y1, x2, y2, cls="aline", m="a", dash=False):
        d = ' stroke-dasharray="5 4"' if dash else ""
        o.append(f'<line class="{cls}" x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}" stroke-width="2"{d} marker-end="url(#{m})"/>')

    def label(x, y, text, cls="ink2", anchor="middle"):
        o.append(f'<text class="{cls}" x="{x}" y="{y}" font-size="11" text-anchor="{anchor}">{text}</text>')

    # host
    box(20, 70, 150, 110, "Linux host", ("gud DRM driver", "hid-multitouch", "/dev/dri/cardN"))
    # ESP32-S3 container
    o.append('<rect class="box" x="220" y="30" width="480" height="270" rx="8" stroke-width="1.2" fill-opacity="0"/>')
    o.append('<text class="ink" x="236" y="52" font-size="13" font-weight="600">ESP32-S3</text>')
    box(240, 70, 120, 70, "USB task", ("core 1, TinyUSB",), size=12)
    box(395, 70, 120, 70, "2 × RX buffer", ("PSRAM, 150 KB each",), size=12)
    box(550, 70, 130, 70, "Render task", ("core 0",), cls="accent", size=12)
    box(550, 165, 130, 50, "LZ4 → 64 KB ring", (), cls="accent", size=11)
    box(395, 165, 120, 50, "byte swap", (), cls="accent", size=11)
    box(395, 235, 120, 50, "2 × DMA 16 KB", (), cls="accent", size=11)
    box(240, 220, 120, 60, "Touch task", ("core 1, every 5 ms",), size=12)
    # panel + touch chip
    box(740, 70, 140, 110, "ILI9341V", ("2.8\", 320 × 240", "RGB565, own GRAM"))
    box(740, 220, 140, 60, "FT6336G", ("2-point touch",), size=12)

    # data path
    arrow(170, 105, 238, 105)
    label(204, 96, "USB FS")
    label(204, 122, "LZ4 rects")
    arrow(360, 105, 393, 105)
    arrow(515, 105, 548, 105)
    arrow(615, 140, 615, 163)
    arrow(550, 190, 517, 190)
    arrow(455, 215, 455, 233)
    o.append('<path class="aline" d="M515,260 H720 V150 H738" fill="none" stroke-width="2" marker-end="url(#a)"/>')
    label(728, 205, "SPI", anchor="end")
    label(728, 219, "40 MHz", anchor="end")
    # touch path
    o.append('<path class="tline" d="M738,265 H705 V292 H300 V282" fill="none" stroke-width="2" stroke-dasharray="5 4" marker-end="url(#t)"/>')
    label(560, 284, "I2C", cls="mut")
    o.append('<path class="tline" d="M240,250 H200 V160 H172" fill="none" stroke-width="2" stroke-dasharray="5 4" marker-end="url(#t)"/>')
    label(196, 210, "HID", cls="mut", anchor="end")
    o.append('</svg>')
    return "\n".join(o)


os.makedirs(OUT, exist_ok=True)
for name, fn in (("fps.svg", fps_svg), ("pipeline.svg", pipeline_svg)):
    with open(os.path.join(OUT, name), "w", encoding="utf-8", newline="\n") as f:
        f.write(fn() + "\n")
print("ok")
