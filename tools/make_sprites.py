#!/usr/bin/env python3
"""Turn the drawings in assets/src into the pictures the screensaver builds in,
under assets/art.

    tools/make_sprites.py

objects.jpeg is a sheet on flat magenta: two bare trees on the top row, and on the
bottom row a green leaf, two more leaves that are not used, a stone and the cutting
frame. The golden leaf and the dried leaf are made from the green one by changing
its colour, so that a leaf keeps exactly its shape as it turns and dries.
fraa.jpeg is four drawings of the fraa in a 2 x 2 grid, of which the last two are
used; anim.jpeg is three frames of him walking with his basket, the basket, and
four frames of him picking a leaf. grove.jpeg
is the background. Needs ImageMagick and numpy.
"""
import subprocess
import numpy as np

SRC, OUT = "assets/src", "assets/art"
def load(path):
    w, h = map(int, subprocess.check_output(["magick", "identify", "-format", "%w %h", path]).split())
    raw = subprocess.check_output(["magick", path, "-depth", "8", "RGB:-"])
    return np.frombuffer(raw, np.uint8).reshape(h, w, 3).astype(np.float32)

def key(img):
    """Magenta background to alpha, and take its colour cast off the edges."""
    r, g, b = img[..., 0].copy(), img[..., 1].copy(), img[..., 2].copy()
    spill = np.minimum(r, b) - g
    cut = np.clip(spill, 0, None) * 0.92
    r, b = r - cut, b - cut
    alpha = np.clip(1.0 - (spill - 24) / 70.0, 0, 1)
    return np.dstack([r, g, b, alpha * 255]).clip(0, 255).astype(np.uint8)

def runs(on, gap):
    """Stretches where `on` is true, joining ones separated by less than `gap`."""
    out, start, last = [], None, None
    for i, v in enumerate(on):
        if not v: continue
        if start is None: start = i
        elif i - last > gap: out.append((start, last + 1)); start = i
        last = i
    if start is not None: out.append((start, last + 1))
    return [r for r in out if r[1] - r[0] > 30]

def pieces(a, rows):
    """The separate drawings on a sheet, in reading order: split into rows by the
    empty bands between them, then each row into drawings by the empty gaps."""
    solid = a[..., 3] > 128
    bands = runs(solid.sum(axis=1) > 2, 40)
    if len(bands) != rows:
        raise SystemExit(f"found {len(bands)} rows of drawings, expected {rows}")
    out = []
    for y0, y1 in bands:
        for x0, x1 in runs(solid[y0:y1].sum(axis=0) > 2, 40):
            out.append(a[y0:y1, x0:x1])
    return out

def trim(a):
    solid = a[..., 3] > 128
    rows = np.where(solid.sum(axis=1) > 2)[0]; cols = np.where(solid.sum(axis=0) > 2)[0]
    return a[rows.min():rows.max() + 1, cols.min():cols.max() + 1]

def save(rgba, w, h, path):
    subprocess.run(["magick", "-size", f"{rgba.shape[1]}x{rgba.shape[0]}", "-depth", "8", "RGBA:-",
                    "-filter", "Lanczos", "-resize", f"{w}x{h}!", "-define", "png:compression-level=9", path],
                   input=np.ascontiguousarray(rgba).tobytes(), check=True)

def main():
    import os
    subprocess.run(["mkdir", "-p", OUT], check=True)
    got = [trim(p) for p in pieces(key(load(f"{SRC}/objects.jpeg")), 2)]
    if len(got) != 7:
        raise SystemExit(f"objects: found {len(got)} drawings, expected 7")
    for name, p, wide in [("tree1", got[0], 760), ("tree2", got[1], 760), ("leaf_g", got[2], 150), ("stone", got[5], 260), ("frame", got[6], 300)]:
        save(p, wide, max(1, round(p.shape[0] * wide / p.shape[1])), f"{OUT}/{name}.png")
    # the same leaf in autumn, and dried: only its colour is changed
    subprocess.run(["magick", f"{OUT}/leaf_g.png", "-modulate", "122,150,80", f"{OUT}/leaf_y.png"], check=True)
    subprocess.run(["magick", f"{OUT}/leaf_g.png", "-modulate", "122,50,84", "-fill", "#f2dba4", "-colorize", "52", f"{OUT}/leaf_d.png"], check=True)
    print("trees, leaves, stone, frame")
    # the fraa: writing and standing from one sheet, and from the other three frames of
    # walking with his basket, the basket, and four frames of picking a leaf. They are
    # all brought to one scale by matching the height at which he stands.
    got = [trim(p) for p in pieces(key(load(f"{SRC}/fraa.jpeg")), 2)]
    if len(got) != 4:
        raise SystemExit(f"fraa: found {len(got)} drawings, expected 4")
    f = 480 / got[3].shape[0]
    for name, p in [("f_write", got[2]), ("f_stand", got[3])]:
        save(p, max(1, round(p.shape[1] * f)), max(1, round(p.shape[0] * f)), f"{OUT}/{name}.png")
    anim = [trim(p) for p in pieces(key(load(f"{SRC}/anim.jpeg")), 2)]
    if len(anim) != 8:
        raise SystemExit(f"anim: found {len(anim)} drawings, expected 8")
    k = 480 * 0.94 / anim[6].shape[0]  # in the seventh he stands flat, a little bowed over his leaf
    for name, p in zip(["w1", "w2", "w3", "basket", "p1", "p2", "p3", "p4"], anim):
        w, h = max(1, round(p.shape[1] * k)), max(1, round(p.shape[0] * k))
        save(p, w, h, f"{OUT}/{name}.png")
        solid = p[..., 3] > 128
        feet = np.where(solid[int(p.shape[0] * 0.95):].any(axis=0))[0]
        head = np.where(solid[:int(p.shape[0] * 0.2)].any(axis=0))[0]
        print(f"{name} {w}x{h}  feet {feet.min() / p.shape[1]:.2f}-{feet.max() / p.shape[1]:.2f}  head {head.mean() / p.shape[1]:.2f}")
    print("fraa")
    subprocess.run(["magick", f"{SRC}/grove.jpeg", "-resize", "1920x1080!", "-quality", "90", f"{OUT}/bg.jpg"], check=True)
    print("grove")

if __name__ == "__main__":
    main()
