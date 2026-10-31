"""Shared texture sets and materials for the generated zone models."""
import os
import numpy as np
from PIL import Image, ImageDraw, ImageFont, ImageFilter
from . import tex

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', '..', '..'))  # assets/
PBR = os.path.join(ROOT, 'textures', 'pbr')
ZONE = os.path.join(ROOT, 'models', 'zone')
TEX_OUT = os.path.join(ZONE, 'textures')

# name -> (source set, pack kwargs)
SETS = {
    'rust': ('rust_coarse_01', dict(metal=0.35)),
    'rust_dark': ('rusty_metal_04', dict(metal=0.3, tint=(0.7, 0.62, 0.55))),
    'rust_sheet': ('rusty_metal_sheet', dict(metal=0.4)),
    'paint_green': ('green_metal_rust', dict(metal=0.2, saturation=0.75)),
    'paint_blue': ('blue_metal_plate', dict(metal=0.2, saturation=0.8, overlay='rust_coarse_01',
                                            overlay_mask=tex.noise_mask(3, 96, 0.5, 0.15))),
    'paint_cream': ('rusty_metal_02', dict(metal=0.2, tint=(1.0, 0.95, 0.82), saturation=0.6, overlay='rust_coarse_01',
                                           overlay_mask=tex.noise_mask(5, 80, 0.55, 0.12))),
    'paint_red': ('blue_metal_plate', dict(metal=0.2, tint=(1.7, 0.55, 0.45), saturation=0.0, overlay='rust_coarse_01',
                                           overlay_mask=tex.noise_mask(7, 96, 0.48, 0.15))),
    'concrete': ('concrete_wall_008', dict()),
    'concrete_dark': ('concrete_layers_02', dict()),
    'concrete_floor': ('concrete_floor_02', dict()),
    'concrete_moss': ('concrete_wall_007', dict(overlay='mossy_rock', overlay_mask=tex.noise_mask(11, 128, 0.62, 0.1))),
    'brick': ('brick_wall_001', dict()),
    'brick_red': ('red_brick', dict(saturation=0.8)),
    'plaster': ('damaged_plaster', dict(saturation=0.8)),
    'plaster_green': ('painted_plaster_wall', dict(tint=(0.78, 0.9, 0.8))),
    'plaster_blue': ('painted_plaster_wall', dict(tint=(0.72, 0.8, 0.9))),
    'tiles': ('large_grey_tiles', dict()),
    'tiles_floor': ('floor_tiles_06', dict(saturation=0.45, tint=(0.72, 0.7, 0.64))),     # trodden, dusty
    'wood': ('weathered_planks', dict()),
    'wood_floor': ('wood_floor_worn', dict(saturation=0.7)),
    'wood_dark': ('dark_wooden_planks', dict()),
    'rubber': ('rubber_tiles', dict()),
    'gravel': ('gravel_concrete_03', dict()),
    'metal_plate': ('metal_plate_02', dict(metal=0.5)),
    'corrugated': ('corrugated_iron_02', dict(metal=0.4)),
    'asphalt': ('asphalt_02', dict()),
    'mud': ('brown_mud_03', dict()),
    'leafy': ('leafy_grass', dict(saturation=1.1)),
    'bark_dark': ('bark_brown_02', dict(tint=(0.62, 0.58, 0.54), saturation=0.6)),
    'roof_tar': ('grey_roof_01', dict(overlay='mossy_rock', overlay_mask=tex.noise_mask(13, 128, 0.6, 0.12))),
    # Skrunda-1 (real photo scans, Poly Haven CC0)
    'silicate': ('whitewashed_brick', dict(saturation=0.3, tint=(1.1, 1.1, 1.08))),     # pale grey sand-lime brick (photos of Skrunda-1)
    'silicate_dirty': ('whitewashed_brick', dict(saturation=0.25, tint=(0.98, 0.98, 0.96))),
    'panel': ('concrete_slab_wall_02', dict()),
    'plaster_mossy': ('worn_mossy_plasterwall', dict()),
    'plaster_yellow': ('yellow_plaster', dict(saturation=0.6, tint=(1.02, 1.0, 0.94))),
    'plaster_peel': ('worn_plaster_wall', dict()),
    'wallpaper': ('decrepit_wallpaper', dict()),
    'stair_paint': ('blue_plaster_weathered', dict(saturation=0.65)),
    'road': ('aerial_asphalt_01', dict(saturation=0.35, tint=(0.88, 0.85, 0.78))),     # old sun-bleached asphalt, sand in it
    'road_worn': ('asphalt_04', dict()),
    'ground_grass': ('grass_ground', dict(tint=(0.85, 0.95, 0.75))),
    'ground_lush': ('sparse_grass', dict(tint=(0.95, 1.0, 0.9))),
    'ground_dry': ('withered_grass', dict(tint=(0.74, 0.8, 0.56), saturation=0.6)),
    'ground_leaves': ('leaves_forest_ground', dict(saturation=0.8)),
    'ground_dirt': ('forest_ground_05', dict(tint=(0.8, 0.85, 0.72), saturation=0.7)),
    'ground_path': ('stony_dirt_path', dict()),
    'ground_ruts': ('aerial_mud_1', dict()),
    'shutter': ('rusty_metal_shutter', dict(metal=0.3)),
    'corr_rust': ('rusty_corrugated_iron', dict(metal=0.35)),
    'debris': ('concrete_debris', dict()),
    'broken_wall': ('rebar_reinforced_concrete', dict(saturation=0.5)),
}


def ensure_textures():
    for name, (src, kw) in SETS.items():
        tex.pack(PBR, TEX_OUT, name, src, **kw)
    make_sign_textures()
    make_facade_texture()
    make_birch_texture()


def _flat_maps(name, rough=0.7, metal=0.0, size=(512, 512)):
    n = Image.new('RGB', size, (128, 128, 255))
    n.save(f'{TEX_OUT}/{name}_normal.jpg', quality=90)
    Image.new('RGB', size, (255, int(rough * 255), int(metal * 255))).save(f'{TEX_OUT}/{name}_orm.jpg', quality=90)


def _font(size, bold=True):
    f = '/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf' if bold else '/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf'
    return ImageFont.truetype(f, size)


def _weather(im, seed, rust=0.35):
    """Fade, dirt and rust streaks over a sign image."""
    rng = np.random.default_rng(seed)
    a = np.asarray(im, dtype=np.float32) / 255
    g = a.mean(axis=2, keepdims=True)
    a = g + (a - g) * 0.7
    h, w = a.shape[:2]
    m = tex.noise_mask(seed, 48, 0.62, 0.2)(max(h, w), max(h, w))[:h, :w]
    rustc = np.array([0.36, 0.2, 0.1], np.float32)
    a = a * (1 - m[..., None] * rust * 2) + rustc * m[..., None] * rust * 2
    for _ in range(40):  # vertical streaks
        x = rng.integers(0, w)
        y0 = rng.integers(0, h // 2)
        ln = rng.integers(h // 6, h)
        a[y0:y0 + ln, max(0, x - 1):x + 2] *= 0.82
    return Image.fromarray(np.clip(a * 255, 0, 255).astype(np.uint8))


def make_sign_textures():
    """Cyrillic signage (fictional places), weathered. One 2048x512 atlas row per sign."""
    path = f'{TEX_OUT}/signs_albedo.jpg'
    if os.path.exists(path):
        return
    W, H = 2048, 2048
    im = Image.new('RGB', (W, H), (40, 40, 40))
    d = ImageDraw.Draw(im)
    # row 0: blue highway direction sign (motorway style)
    d.rectangle([0, 0, W, 512], fill=(24, 70, 140))
    d.rectangle([14, 14, W - 14, 498], outline=(235, 235, 235), width=10)
    d.text((70, 60), 'Заводск', font=_font(150), fill=(240, 240, 240))
    d.text((70, 260), 'Красная Падь  14', font=_font(120), fill=(240, 240, 240))
    d.text((1520, 60), 'Е-95', font=_font(130), fill=(240, 200, 40))
    d.text((1560, 300), '↑', font=_font(170), fill=(240, 240, 240))
    # row 1: green direction sign
    d.rectangle([0, 512, W, 1024], fill=(20, 100, 60))
    d.rectangle([14, 526, W - 14, 1010], outline=(235, 235, 235), width=10)
    d.text((70, 570), 'ГРЭС-3', font=_font(150), fill=(240, 240, 240))
    d.text((70, 770), 'Промзона  ←', font=_font(120), fill=(240, 240, 240))
    d.text((1300, 770), 'Центр  →', font=_font(120), fill=(240, 240, 240))
    # row 2: hospital sign (white letters on red brick-like band)
    d.rectangle([0, 1024, W, 1536], fill=(225, 222, 210))
    d.text((80, 1090), 'БОЛЬНИЦА', font=_font(210), fill=(150, 30, 25))
    d.text((90, 1350), 'Приёмное отделение', font=_font(110), fill=(60, 60, 60))
    d.rectangle([1720, 1110, 1790, 1420], fill=(190, 30, 25))
    d.rectangle([1600, 1230, 1910, 1300], fill=(190, 30, 25))
    # row 3: left half: station sign, right half: warning sign
    d.rectangle([0, 1536, W // 2, H], fill=(230, 228, 215))
    d.text((60, 1640), 'ст. Заводск', font=_font(140), fill=(30, 30, 30))
    d.text((60, 1860), 'Товарная', font=_font(100), fill=(30, 30, 30))
    d.rectangle([W // 2, 1536, W, H], fill=(235, 200, 30))
    d.polygon([(W // 2 + 512, 1590), (W // 2 + 120, 2000), (W - 120, 2000)], outline=(20, 20, 20), width=24)
    d.text((W // 2 + 470, 1720), '!', font=_font(220), fill=(20, 20, 20))
    d.text((W // 2 + 40, 1560), 'ОПАСНО', font=_font(80), fill=(20, 20, 20))
    im = _weather(im, 21, 0.3)
    im.save(path, quality=88)
    _flat_maps('signs', rough=0.6, metal=0.1, size=(256, 256))


def make_facade_texture():
    """Panel high-rise facade tile: 4 panels x 4 storeys (12 m x 12 m), dark windows, some balconies."""
    path = f'{TEX_OUT}/facade_albedo.jpg'
    if os.path.exists(path):
        return
    S = 1024
    rng = np.random.default_rng(4)
    base = Image.open(f'{PBR}/concrete_wall_008/albedo.jpg').convert('RGB').resize((S, S))
    a = np.asarray(base, dtype=np.float32) / 255 * np.array([1.0, 0.98, 0.93])
    rough = np.full((S, S), 0.9, np.float32)
    px = S / 12.0  # pixels per metre
    for fy in range(4):
        for fx in range(4):
            x0 = int((fx * 3 + 0.7) * px)
            x1 = int((fx * 3 + 2.3) * px)
            y0 = int((fy * 3 + 0.8) * px)
            y1 = int((fy * 3 + 2.3) * px)
            dark = rng.uniform(0.02, 0.09)
            a[y0:y1, x0:x1] = dark
            if rng.random() < 0.6:  # remaining glass shard / frame
                a[y0:y1, x0 + (x1 - x0) // 2 - 2:x0 + (x1 - x0) // 2 + 2] = 0.25
            rough[y0:y1, x0:x1] = 0.3
            # panel joints
        a[int(fy * 3 * px):int(fy * 3 * px) + 3, :] *= 0.55
    for fx in range(5):
        a[:, max(0, int(fx * 3 * px) - 2):int(fx * 3 * px) + 2] *= 0.6
    # grime streaks below windows
    for _ in range(300):
        x = rng.integers(0, S)
        y = rng.integers(0, S)
        a[y:y + rng.integers(20, 160), x:x + 3] *= rng.uniform(0.75, 0.92)
    Image.fromarray(np.clip(a * 255, 0, 255).astype(np.uint8)).save(path, quality=88)
    n = Image.open(f'{PBR}/concrete_wall_008/normal.jpg').convert('RGB').resize((S, S))
    n.save(f'{TEX_OUT}/facade_normal.jpg', quality=88)
    orm = np.stack([np.ones_like(rough), rough, np.zeros_like(rough)], axis=2)
    Image.fromarray((orm * 255).astype(np.uint8)).save(f'{TEX_OUT}/facade_orm.jpg', quality=88)


def make_birch_texture():
    """Birch bark: chalky white with dark horizontal lenticels and black patches near knots."""
    path = f'{TEX_OUT}/birch_albedo.jpg'
    if os.path.exists(path):
        return
    S = 512
    rng = np.random.default_rng(8)
    a = np.ones((S, S, 3), np.float32) * np.array([0.82, 0.8, 0.74], np.float32)
    a *= (0.88 + 0.12 * tex.noise_mask(2, 32, 0.5, 1.0)(S, S))[..., None]
    for _ in range(260):
        x, y = rng.integers(0, S, 2)
        w, h = rng.integers(8, 60), rng.integers(2, 6)
        xs = (np.arange(x, x + w) % S)
        a[y:y + h, xs] = rng.uniform(0.08, 0.3)
    for _ in range(14):
        x, y = rng.integers(0, S, 2)
        r = rng.integers(10, 28)
        yy, xx = np.ogrid[:S, :S]
        mask = ((xx - x) ** 2 / (r * 1.6) ** 2 + (yy - y) ** 2 / r ** 2) < 1
        a[mask] = 0.07
    Image.fromarray(np.clip(a * 255, 0, 255).astype(np.uint8)).save(path, quality=88)
    _flat_maps('birch', rough=0.85, metal=0.0, size=(256, 256))


def M(texname=None, tile=2.0, color=(1, 1, 1), rough=1.0, metal=1.0, **kw):
    """Material dict. With an ORM texture the factors multiply the map (glTF), so keep 1.0."""
    d = dict(tex=texname, tile=tile, color=color, rough=rough, metal=metal)
    d.update(kw)
    return d


# Common material palette (name -> material). Generators pick what they need.
PALETTE = {
    'rust': M('rust', 1.5),
    'rust_dark': M('rust_dark', 1.5),
    'rust_sheet': M('rust_sheet', 2.0),
    'paint_green': M('paint_green', 1.6),
    'paint_blue': M('paint_blue', 1.6),
    'paint_cream': M('paint_cream', 1.6),
    'paint_red': M('paint_red', 1.6),
    'burnt': M('rust_dark', 1.2, color=(0.35, 0.3, 0.28)),
    'concrete': M('concrete', 3.0),
    'concrete_dark': M('concrete_dark', 3.0),
    'concrete_floor': M('concrete_floor', 3.0),
    'concrete_moss': M('concrete_moss', 4.0),
    'brick': M('brick', 2.0),
    'brick_red': M('brick_red', 1.4),
    'plaster': M('plaster', 2.5),
    'plaster_green': M('plaster_green', 2.5),
    'plaster_blue': M('plaster_blue', 2.5),
    'tiles': M('tiles', 1.0),
    'tiles_floor': M('tiles_floor', 1.2),
    'wood': M('wood', 1.5),
    'wood_floor': M('wood_floor', 2.0),
    'wood_dark': M('wood_dark', 1.5),
    'rubber': M('rubber', 0.6, color=(0.35, 0.35, 0.35)),
    'gravel': M('gravel', 2.0),
    'metal_plate': M('metal_plate', 1.5),
    'corrugated': M('corrugated', 2.0),
    'asphalt': M('asphalt', 4.0),
    'mud': M('mud', 3.0),
    'leafy': ('leafy_grass', dict(saturation=1.1)),
    'bark_dark': ('bark_brown_02', dict(tint=(0.62, 0.58, 0.54), saturation=0.6)),
    'roof_tar': M('roof_tar', 4.0),
    'facade': M('facade', 12.0),
    'interior_dark': M('concrete_dark', 3.0, color=(0.25, 0.25, 0.25)),
    'signs': M('signs', 1.0),
    'bark_birch': M('birch', 1.2),
    'bark_dead': M('bark_dark', 1.0),
    'leaves': M('leafy', 1.5, color=(0.62, 0.78, 0.45)),
    'leaves_dark': M('leafy', 1.5, color=(0.45, 0.58, 0.32)),
    'grass': M('leafy', 1.0, color=(0.7, 0.85, 0.5)),
    'grass_dry': M('leafy', 1.0, color=(1.1, 0.95, 0.55)),
    'silicate': M('silicate', 2.0),
    'silicate_dirty': M('silicate_dirty', 1.5),
    'panel': M('panel', 2.1),
    'plaster_mossy': M('plaster_mossy', 1.8),
    'plaster_yellow': M('plaster_yellow', 2.0),
    'plaster_peel': M('plaster_peel', 1.8),
    'wallpaper': M('wallpaper', 2.5),
    'stair_paint': M('stair_paint', 2.0),
    'road': M('road', 9.0),
    'road_worn': M('road_worn', 4.0),
    'ground_grass': M('ground_grass', 2.5),
    'ground_lush': M('ground_lush', 2.0),
    'ground_dry': M('ground_dry', 2.0),
    'ground_leaves': M('ground_leaves', 1.3),
    'ground_dirt': M('ground_dirt', 2.0),
    'ground_path': M('ground_path', 2.2),
    'ground_ruts': M('ground_ruts', 8.0),
    'shutter': M('shutter', 1.9),
    'corr_rust': M('corr_rust', 2.0),
    'debris': M('debris', 2.0),
    'broken_wall': M('broken_wall', 2.0),
    'glass': M(None, 1.0, color=(0.05, 0.06, 0.06), rough=0.08, metal=0.0, alpha=0.35),
    'water': M(None, 4.0, color=(0.012, 0.014, 0.012), rough=0.1, metal=0.0),
    'lamp': M(None, 1.0, color=(1.0, 0.8, 0.5), rough=0.5, metal=0.0, emissive=(1.0, 0.62, 0.3)),
}
