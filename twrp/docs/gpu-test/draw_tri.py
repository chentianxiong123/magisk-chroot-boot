import struct

W, H = 1080, 1920
STRIDE = 4352  # bytes per row
PIXEL = 4      # 32bpp BGRA (MTK little-endian)

# fb0 数据（黑底 + 3 个彩色三角形 + 白色边框）
buf = bytearray(STRIDE * H)

def set_pixel(x, y, r, g, b):
    if 0 <= x < W and 0 <= y < H:
        offset = y * STRIDE + x * PIXEL
        # BGRA little-endian (MTK fbdev 一般是 BGRX)
        buf[offset:offset+4] = struct.pack('<BBBB', b, g, r, 0xff)

# 黑底
for y in range(H):
    for x in range(W):
        set_pixel(x, y, 0, 0, 0)

# 3 个三角形
def draw_triangle(vertices, r, g, b):
    (x0, y0), (x1, y1), (x2, y2) = vertices
    # 简单扫描线填充
    ymin = max(0, min(y0, y1, y2))
    ymax = min(H-1, max(y0, y1, y2))
    for y in range(ymin, ymax+1):
        xs = []
        # 找 3 条边的交点
        for (ax, ay), (bx, by) in [((x0,y0),(x1,y1)), ((x1,y1),(x2,y2)), ((x2,y2),(x0,y0))]:
            if ay != by:
                t = (y - ay) / (by - ay)
                if 0 <= t <= 1:
                    xs.append(ax + t * (bx - ax))
        if len(xs) >= 2:
            xmin = max(0, int(min(xs)))
            xmax = min(W-1, int(max(xs)))
            for x in range(xmin, xmax+1):
                set_pixel(x, y, r, g, b)

# 红、绿、蓝 3 个三角形
draw_triangle([(540, 200), (240, 800), (840, 800)], 255, 0, 0)
draw_triangle([(540, 600), (240, 1200), (840, 1200)], 0, 255, 0)
draw_triangle([(540, 1000), (240, 1600), (840, 1600)], 0, 0, 255)

# 白色边框
for x in range(W):
    set_pixel(x, 0, 255, 255, 255)
    set_pixel(x, H-1, 255, 255, 255)
for y in range(H):
    set_pixel(0, y, 255, 255, 255)
    set_pixel(W-1, y, 255, 255, 255)

# 顶部文字区（白条，显示这是 framebuffer 输出）
for y in range(100):
    for x in range(W):
        set_pixel(x, y, 128, 128, 128)

with open('fb_tri.rgb', 'wb') as f:
    f.write(buf)

print(f"生成 fb_tri.rgb: {len(buf)} bytes")
print(f"分辨率: {W}x{H}, stride: {STRIDE}, bpp: {PIXEL*8}")
