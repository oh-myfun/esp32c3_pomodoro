"""把 simulator 输出的多帧 PNG 合成 GIF。

用法（在项目根目录）：
    uv run --with pillow tools/simulator/make_gif.py
"""
import os
import sys
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
FRAMES_DIR = os.path.join(ROOT, "tools", "simulator", "output", "gif_frames")
OUT_GIF = os.path.join(ROOT, "docs", "images", "buddy-celebrate.gif")

FRAME_COUNT = 16
DURATION_MS = 200
SCALE = 2  # 240 -> 480 for clearer preview


def main():
    if not os.path.isdir(FRAMES_DIR):
        sys.exit(f"frames dir not found: {FRAMES_DIR}\nrun sim.exe first")

    files = sorted(f for f in os.listdir(FRAMES_DIR) if f.endswith(".png"))[:FRAME_COUNT]
    if not files:
        sys.exit("no PNG frames")

    print(f"Loading {len(files)} frames from {FRAMES_DIR}")
    frames = []
    for f in files:
        im = Image.open(os.path.join(FRAMES_DIR, f)).convert("P", palette=Image.ADAPTIVE, colors=255)
        if SCALE != 1:
            im = im.resize((240 * SCALE, 240 * SCALE), Image.NEAREST)
        frames.append(im)

    os.makedirs(os.path.dirname(OUT_GIF), exist_ok=True)
    frames[0].save(
        OUT_GIF,
        save_all=True,
        append_images=frames[1:],
        duration=DURATION_MS,
        loop=0,
        disposal=2,
    )
    print(f"Wrote {OUT_GIF}  ({os.path.getsize(OUT_GIF)} bytes, {len(frames)} frames, {DURATION_MS}ms/frame)")


if __name__ == "__main__":
    main()
