"""Packs Poly Haven 2K sets (assets/textures/pbr/<id>) into shared 1K glTF textures:
<name>_albedo.jpg, <name>_normal.jpg (OpenGL, same as the source) and <name>_orm.jpg
(R = ambient occlusion, G = roughness, B = metallic) in assets/models/zone/textures/.
Optional tint/rust blending makes painted-metal variants from one source set.
"""
import os
import numpy as np
from PIL import Image

SIZE = 1024


def _load(path, mode='RGB'):
    im = Image.open(path)
    im.draft(mode, (SIZE, SIZE))
    return im.convert(mode).resize((SIZE, SIZE), Image.LANCZOS)


def pack(pbr_dir, out_dir, name, src, metal=0.0, tint=None, saturation=1.0, overlay=None, overlay_mask=None):
    """src: Poly Haven id. tint: RGB multiplier (0-1). overlay: second set id blended in by
    overlay_mask (a function(h,w) -> 0..1 array) for rust over paint."""
    os.makedirs(out_dir, exist_ok=True)
    targets = [f'{out_dir}/{name}_{k}.jpg' for k in ('albedo', 'normal', 'orm')]
    if all(os.path.exists(t) for t in targets):
        return
    d = f'{pbr_dir}/{src}'
    alb = np.asarray(_load(f'{d}/albedo.jpg'), dtype=np.float32) / 255
    nrm = np.asarray(_load(f'{d}/normal.jpg'), dtype=np.float32) / 255
    rough = np.asarray(_load(f'{d}/roughness.jpg', 'L'), dtype=np.float32) / 255
    ao = np.asarray(_load(f'{d}/ao.jpg', 'L'), dtype=np.float32) / 255 if os.path.exists(f'{d}/ao.jpg') else np.ones_like(rough)
    met = np.full_like(rough, metal)
    if os.path.exists(f'{d}/metallic.jpg'):
        met = np.asarray(_load(f'{d}/metallic.jpg', 'L'), dtype=np.float32) / 255
    if saturation != 1.0:
        g = alb.mean(axis=2, keepdims=True)
        alb = g + (alb - g) * saturation
    if tint is not None:
        alb = alb * np.asarray(tint, dtype=np.float32)
    if overlay:
        o = f'{pbr_dir}/{overlay}'
        oa = np.asarray(_load(f'{o}/albedo.jpg'), dtype=np.float32) / 255
        on = np.asarray(_load(f'{o}/normal.jpg'), dtype=np.float32) / 255
        orr = np.asarray(_load(f'{o}/roughness.jpg', 'L'), dtype=np.float32) / 255
        m = overlay_mask(SIZE, SIZE).astype(np.float32)
        alb = alb * (1 - m[..., None]) + oa * m[..., None]
        nrm = nrm * (1 - m[..., None]) + on * m[..., None]
        rough = rough * (1 - m) + orr * m
        met = met * (1 - m)
    orm = np.stack([ao, rough, met], axis=2)
    save = lambda a, p: Image.fromarray(np.clip(a * 255 + 0.5, 0, 255).astype(np.uint8)).save(p, quality=88)
    save(np.clip(alb, 0, 1), targets[0])
    save(nrm, targets[1])
    save(orm, targets[2])


def noise_mask(seed, scale=64, threshold=0.5, softness=0.12):
    """Blotchy tileable mask (value noise, summed octaves)."""
    def f(h, w):
        rng = np.random.default_rng(seed)
        acc = np.zeros((h, w), np.float32)
        amp, tot = 1.0, 0.0
        cells = max(2, w // scale)
        for _ in range(4):
            g = rng.random((cells, cells)).astype(np.float32)
            gp = np.pad(g, 2, mode='wrap')
            cpx = w / cells
            big = Image.fromarray((gp * 255).astype(np.uint8)).resize((int(round(cpx * (cells + 4))),) * 2, Image.BICUBIC)
            o = int(round(cpx * 2))
            acc += amp * np.asarray(big, dtype=np.float32)[o:o + h, o:o + w] / 255
            tot += amp
            amp *= 0.5
            cells *= 2
        acc /= tot
        return np.clip((acc - threshold) / softness + 0.5, 0, 1)
    return f
