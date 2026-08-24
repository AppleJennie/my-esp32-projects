#!/usr/bin/env python3
"""
Generate LVGL font file (v8, 4bpp) from a TTF font.
"""
import struct
import os
from PIL import Image, ImageDraw, ImageFont

# Configuration
FONT_PATH = r"C:\Windows\Fonts\simhei.ttf"
FONT_SIZE = 20
BPP = 4
OUTPUT_PATH = r"Software\OV_Watch\User\GUI_App\Fonts\ui_font_Cuyuan20_new.c"

# Characters to include
# ASCII full range
ascii_chars = [chr(c) for c in range(0x20, 0x7F)]

# Original Cuyuan20 Chinese characters (from --symbols)
original_chinese = (
    "同步APP常亮熄屏时间日期抬腕亮屏密码设置校园门禁交通卡日历计算器"
    "秒表卡包运动心率血氧环境指南针设置关于次分正在测量手表型号固件"
    "型号主控芯片操作系统图形界面库硬件软件开发者滑机游戏重玩记忆方块反弹球"
)

# New characters needed for sleep pages + other fixes
new_chinese = (
    "睡眠报告评开始监测结束未总深浅清醒返回主页"
    "仰左右俯参考价值非医用"
    "姿翻身平均率最低卧侧知"
)

# Build unique character set
all_chars_set = set(ascii_chars)
for c in original_chinese:
    if ord(c) >= 0x80:
        all_chars_set.add(c)
for c in new_chinese:
    if ord(c) >= 0x80:
        all_chars_set.add(c)

all_chars = sorted(all_chars_set, key=lambda c: ord(c))
print(f"Total glyphs: {len(all_chars)}")

# Load font
font = ImageFont.truetype(FONT_PATH, FONT_SIZE)

# Metrics
def get_glyph_metrics(char):
    """Render character and return metrics + bitmap data."""
    # Get bounding box
    bbox = font.getbbox(char)
    if bbox is None:
        return None
    left, top, right, bottom = bbox
    
    # Create image with padding
    img_w = right - left
    img_h = bottom - top
    if img_w <= 0 or img_h <= 0:
        # Space or empty glyph
        adv = font.getlength(char)
        return {
            'adv_w': int(adv * 16),
            'box_w': 0,
            'box_h': 0,
            'ofs_x': 0,
            'ofs_y': 0,
            'bitmap': b''
        }
    
    # Add 2px padding
    padding = 2
    canvas_w = img_w + padding * 2
    canvas_h = img_h + padding * 2
    
    img = Image.new('L', (canvas_w, canvas_h), 0)
    draw = ImageDraw.Draw(img)
    draw.text((padding - left, padding - top), char, font=font, fill=255)
    
    # Crop to actual content
    bbox2 = img.getbbox()
    if bbox2 is None:
        adv = font.getlength(char)
        return {
            'adv_w': int(adv * 16),
            'box_w': 0,
            'box_h': 0,
            'ofs_x': 0,
            'ofs_y': 0,
            'bitmap': b''
        }
    
    cl, ct, cr, cb = bbox2
    box_w = cr - cl
    box_h = cb - ct
    
    # Ensure even width for 4bpp packing
    if box_w % 2 != 0:
        box_w += 1
    
    cropped = img.crop((cl, ct, cl + box_w, ct + box_h))
    
    # Convert to 4bpp
    pixels = list(cropped.getdata())
    bitmap = bytearray()
    for i in range(0, len(pixels), 2):
        p1 = pixels[i] >> 4
        if i + 1 < len(pixels):
            p2 = pixels[i + 1] >> 4
        else:
            p2 = 0
        bitmap.append((p1 << 4) | p2)
    
    adv = font.getlength(char)
    
    # Calculate baseline offset
    # baseline is at FONT_SIZE from top in PIL
    ofs_y = -(FONT_SIZE - bottom)
    ofs_x = cl - padding
    
    return {
        'adv_w': int(adv * 16),
        'box_w': box_w,
        'box_h': box_h,
        'ofs_x': ofs_x,
        'ofs_y': ofs_y,
        'bitmap': bytes(bitmap)
    }

# Generate all glyphs
glyphs = []
for ch in all_chars:
    m = get_glyph_metrics(ch)
    if m is None:
        m = {
            'adv_w': FONT_SIZE * 16,
            'box_w': 0,
            'box_h': 0,
            'ofs_x': 0,
            'ofs_y': 0,
            'bitmap': b''
        }
    glyphs.append(m)

# Build bitmap array
bitmap_data = bytearray()
bitmap_indices = []
for g in glyphs:
    bitmap_indices.append(len(bitmap_data))
    bitmap_data.extend(g['bitmap'])

# Build glyph descriptors
glyph_dsc_lines = []
glyph_dsc_lines.append("static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {")
glyph_dsc_lines.append("    /* Default fallback glyph */")
glyph_dsc_lines.append("    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0},")
for i, g in enumerate(glyphs):
    glyph_dsc_lines.append(
        f"    {{.bitmap_index = {bitmap_indices[i]}, .adv_w = {g['adv_w']}, "
        f".box_w = {g['box_w']}, .box_h = {g['box_h']}, .ofs_x = {g['ofs_x']}, .ofs_y = {g['ofs_y']}}},"
    )
glyph_dsc_lines.append("};")

# Build unicode list for sparse cmap
unicode_offsets = []
range_start = ord(all_chars[0])
for ch in all_chars:
    unicode_offsets.append(ord(ch) - range_start)

# Format unicode list (16 per line)
unicode_lines = []
unicode_lines.append("static const uint16_t unicode_list_0[] = {")
line_items = []
for off in unicode_offsets:
    line_items.append(f"0x{off:04x}")
    if len(line_items) >= 16:
        unicode_lines.append("    " + ", ".join(line_items) + ",")
        line_items = []
if line_items:
    unicode_lines.append("    " + ", ".join(line_items))
unicode_lines.append("};")

# Build cmap
cmap_lines = []
cmap_lines.append("static const lv_font_fmt_txt_cmap_t cmaps[] =")
cmap_lines.append("{")
cmap_lines.append(f"    {{")
cmap_lines.append(f"        .range_start = {range_start}, .range_length = {unicode_offsets[-1] + 1}, .glyph_id_start = 1,")
cmap_lines.append(f"        .unicode_list = unicode_list_0, .glyph_id_ofs_list = NULL, .list_length = {len(unicode_offsets)}, .type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY")
cmap_lines.append(f"    }}")
cmap_lines.append("};")

# Build bitmap array (12 bytes per line)
bitmap_lines = []
bitmap_lines.append("static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {")
line_bytes = []
for i, b in enumerate(bitmap_data):
    line_bytes.append(f"0x{b:02x}")
    if len(line_bytes) >= 12:
        bitmap_lines.append("    " + ", ".join(line_bytes) + ",")
        line_bytes = []
if line_bytes:
    bitmap_lines.append("    " + ", ".join(line_bytes))
bitmap_lines.append("};")

# Assemble output
lines = []
lines.append("/******************************************************************************")
lines.append(" * Size: 20 px")
lines.append(" * Bpp: 4")
lines.append(" * Auto-generated by gen_font.py")
lines.append(" ******************************************************************************/")
lines.append("")
lines.append("#include \"../ui.h\"")
lines.append("")
lines.append("#ifndef UI_FONT_CUYUAN20")
lines.append("#define UI_FONT_CUYUAN20 1")
lines.append("#endif")
lines.append("")
lines.append("#if UI_FONT_CUYUAN20")
lines.append("")
lines.append("/*-----------------")
lines.append(" *    BITMAPS")
lines.append(" *----------------*/")
lines.append("")
lines.extend(bitmap_lines)
lines.append("")
lines.append("/*--------------------")
lines.append(" *  GLYPH DESCRIPTOR")
lines.append(" *--------------------*/")
lines.append("")
lines.extend(glyph_dsc_lines)
lines.append("")
lines.append("/*---------------------")
lines.append(" *  CHARACTER MAPPING")
lines.append(" *--------------------*/")
lines.append("")
lines.extend(unicode_lines)
lines.append("")
lines.extend(cmap_lines)
lines.append("")
lines.append("/*--------------------")
lines.append(" *  ALL CUSTOM DATA")
lines.append(" *--------------------*/")
lines.append("")
lines.append("#if LV_VERSION_CHECK(8, 0, 0)")
lines.append("static lv_font_fmt_txt_glyph_cache_t cache;")
lines.append("static const lv_font_fmt_txt_dsc_t font_dsc = {")
lines.append("#else")
lines.append("static lv_font_fmt_txt_dsc_t font_dsc = {")
lines.append("#endif")
lines.append("    .glyph_bitmap = glyph_bitmap,")
lines.append("    .glyph_dsc = glyph_dsc,")
lines.append("    .cmaps = cmaps,")
lines.append("    .kern_dsc = NULL,")
lines.append("    .kern_scale = 0,")
lines.append("    .cmap_num = 1,")
lines.append("    .bpp = 4,")
lines.append("    .kern_classes = 0,")
lines.append("    .bitmap_format = 0,")
lines.append("#if LV_VERSION_CHECK(8, 0, 0)")
lines.append("    .cache = &cache")
lines.append("#endif")
lines.append("};")
lines.append("")
lines.append("/*-----------------")
lines.append(" *  PUBLIC FONT")
lines.append(" *----------------*/")
lines.append("")
lines.append("#if LV_VERSION_CHECK(8, 0, 0)")
lines.append("const lv_font_t ui_font_Cuyuan20 = {")
lines.append("#else")
lines.append("lv_font_t ui_font_Cuyuan20 = {")
lines.append("#endif")
lines.append("    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,")
lines.append("    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,")
lines.append(f"    .line_height = {FONT_SIZE + 4},")
lines.append(f"    .base_line = 4,")
lines.append("#if !(LVGL_VERSION_MAJOR == 6 && LVGL_VERSION_MINOR == 0)")
lines.append("    .subpx = LV_FONT_SUBPX_NONE,")
lines.append("#endif")
lines.append("#if LV_VERSION_CHECK(7, 4, 0) || LVGL_VERSION_MAJOR >= 8")
lines.append("    .underline_position = -4,")
lines.append("    .underline_thickness = 1,")
lines.append("#endif")
lines.append("    .dsc = &font_dsc")
lines.append("};")
lines.append("")
lines.append("#endif /*#if UI_FONT_CUYUAN20*/")
lines.append("")

with open(OUTPUT_PATH, "w", encoding="utf-8") as f:
    f.write("\n".join(lines))

print(f"Font generated: {OUTPUT_PATH}")
print(f"File size: {os.path.getsize(OUTPUT_PATH)} bytes")
print(f"Bitmap size: {len(bitmap_data)} bytes")
print(f"Glyphs: {len(glyphs)}")
