"""Builds examples/13_platformer/assets: one 48px atlas and five short sounds.

The art is drawn at half size (24x24 per cell) and scaled up 2x with nearest
neighbour, which gives the chunky pixel look.
"""
import math
import os
import struct
import wave

from PIL import Image, ImageDraw

OUT = os.path.join("examples", "13_platformer", "assets")
CELL = 48
LOG = 24
COLS = 8
ROWS = 6

# ------------------------------------------------------------------ palette
GRASS = (110, 190, 80)
GRASS_DARK = (74, 148, 54)
GRASS_LIGHT = (156, 222, 116)
DIRT = (168, 114, 66)
DIRT_DARK = (126, 80, 44)
DIRT_LIGHT = (200, 148, 96)
BRICK = (198, 106, 66)
BRICK_DARK = (146, 68, 42)
BRICK_LIGHT = (234, 160, 112)
MORTAR = (246, 216, 182)
GOLD = (250, 192, 72)
GOLD_DARK = (172, 108, 22)
GOLD_LIGHT = (255, 232, 150)
USED = (146, 104, 70)
USED_DARK = (100, 66, 42)
PIPE = (72, 178, 92)
PIPE_DARK = (38, 118, 56)
PIPE_LIGHT = (146, 224, 156)
WOOD = (156, 100, 54)
WOOD_DARK = (110, 68, 36)
WOOD_LIGHT = (206, 154, 100)
STEEL = (198, 200, 210)
STEEL_LIGHT = (238, 240, 248)
STEEL_DARK = (146, 148, 162)
WHITE = (250, 252, 255)
SHADE = (206, 228, 250)
BUSH = (86, 166, 70)
BUSH_DARK = (58, 126, 50)
BUSH_LIGHT = (126, 200, 100)
SKIN = (246, 202, 162)
SKIN_DARK = (214, 164, 124)
HAIR = (96, 58, 36)
CAP = (255, 206, 66)
CAP_DARK = (208, 152, 28)
SHIRT = (62, 176, 168)
SHIRT_DARK = (34, 132, 128)
PANTS = (58, 78, 158)
PANTS_DARK = (38, 52, 116)
BOOT = (112, 70, 40)
FOE = (156, 98, 62)
FOE_DARK = (108, 64, 38)
FOE_FOOT = (78, 48, 30)
RED = (232, 64, 62)
RED_LIGHT = (255, 128, 120)
BLACK = (32, 26, 34)
CLEAR = (0, 0, 0, 0)


def canvas():
    img = Image.new("RGBA", (LOG, LOG), CLEAR)
    return img, ImageDraw.Draw(img)


def rect(d, x0, y0, x1, y1, color):
    if x1 < x0 or y1 < y0:
        return
    d.rectangle([x0, y0, x1, y1], fill=color)


def disc(d, cx, cy, r, color):
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=color)


# ------------------------------------------------------------------ tiles
def t_ground_top():
    img, d = canvas()
    rect(d, 0, 0, LOG - 1, LOG - 1, DIRT)
    rect(d, 0, 0, LOG - 1, 4, GRASS)
    rect(d, 0, 0, LOG - 1, 1, GRASS_LIGHT)
    # ragged lower edge of the grass
    for x in range(0, LOG, 4):
        rect(d, x, 5, x + 2, 6, GRASS_DARK)
    rect(d, 0, 7, LOG - 1, 7, DIRT_DARK)
    for x, y in ((3, 10), (9, 12), (16, 9), (20, 14), (6, 17), (13, 18), (18, 20), (2, 21)):
        rect(d, x, y, x + 1, y + 1, DIRT_DARK)
    for x, y in ((5, 11), (12, 14), (19, 17), (8, 20)):
        rect(d, x, y, x + 1, y, DIRT_LIGHT)
    return img


def t_ground_fill():
    img, d = canvas()
    rect(d, 0, 0, LOG - 1, LOG - 1, DIRT)
    for x, y in ((2, 3), (10, 5), (18, 2), (5, 9), (15, 11), (21, 8),
                 (8, 15), (17, 16), (3, 18), (12, 20), (20, 21)):
        rect(d, x, y, x + 1, y + 1, DIRT_DARK)
    for x, y in ((6, 4), (14, 6), (20, 12), (4, 13), (11, 17), (16, 19)):
        rect(d, x, y, x + 1, y, DIRT_LIGHT)
    return img


def t_brick():
    img, d = canvas()
    rect(d, 0, 0, LOG - 1, LOG - 1, BRICK)
    for y in (7, 15, 23):
        rect(d, 0, y, LOG - 1, y, MORTAR)
    for i, y in enumerate((0, 8, 16)):
        offset = 0 if i % 2 == 0 else 6
        for x in range(offset, LOG, 12):
            if 0 < x < LOG:
                rect(d, x, y, x, y + 6, MORTAR)
    for x, y in ((3, 2), (15, 2), (9, 10), (21, 10), (5, 18), (17, 18)):
        rect(d, x, y, x + 2, y, BRICK_LIGHT)
    rect(d, 0, 0, LOG - 1, 0, BRICK_LIGHT)
    rect(d, 0, LOG - 1, LOG - 1, LOG - 1, BRICK_DARK)
    return img


def _qblock(glow_x, dot):
    img, d = canvas()
    rect(d, 1, 1, LOG - 2, LOG - 2, GOLD)
    rect(d, 1, 1, LOG - 2, 2, GOLD_LIGHT)
    rect(d, 1, 1, 2, LOG - 2, GOLD_LIGHT)
    rect(d, 1, LOG - 3, LOG - 2, LOG - 2, GOLD_DARK)
    rect(d, LOG - 3, 1, LOG - 2, LOG - 2, GOLD_DARK)
    for x, y in ((3, 3), (19, 3), (3, 19), (19, 19)):
        rect(d, x, y, x + 1, y + 1, GOLD_DARK)
    # shine band first, the question mark sits on top of it
    rect(d, glow_x, 4, glow_x + 1, 16, GOLD_LIGHT)
    rect(d, 9, 6, 14, 7, GOLD_DARK)
    rect(d, 14, 7, 15, 10, GOLD_DARK)
    rect(d, 11, 10, 14, 11, GOLD_DARK)
    rect(d, 11, 11, 12, 13, GOLD_DARK)
    if dot:
        rect(d, 11, 15, 12, 16, GOLD_DARK)
    return img


def t_used():
    img, d = canvas()
    rect(d, 1, 1, LOG - 2, LOG - 2, USED)
    rect(d, 1, 1, LOG - 2, 2, (176, 128, 88))
    rect(d, 1, LOG - 3, LOG - 2, LOG - 2, USED_DARK)
    rect(d, LOG - 3, 1, LOG - 2, LOG - 2, USED_DARK)
    for x, y in ((3, 3), (19, 3), (3, 19), (19, 19)):
        rect(d, x, y, x + 1, y + 1, USED_DARK)
    return img


def t_platform():
    img, d = canvas()
    rect(d, 0, 2, LOG - 1, 20, WOOD)
    rect(d, 0, 2, LOG - 1, 4, WOOD_LIGHT)
    rect(d, 0, 5, LOG - 1, 5, WOOD_DARK)
    for y in (11, 17):
        rect(d, 0, y, LOG - 1, y, WOOD_DARK)
    for x in (4, 12, 20):
        rect(d, x, 3, x, 4, WOOD_DARK)
    rect(d, 0, 21, LOG - 1, LOG - 1, WOOD_DARK)
    return img


def pipe(left, top):
    """One half of a pipe; left and right halves meet without a seam."""
    img, d = canvas()
    body_x0, body_x1 = (4, LOG - 1) if left else (0, LOG - 5)

    if top:
        rect(d, 0, 0, LOG - 1, 7, (98, 198, 114))
        rect(d, 0, 0, LOG - 1, 1, PIPE_LIGHT)
        rect(d, 0, 6, LOG - 1, 7, PIPE_DARK)
        if left:
            rect(d, 0, 0, 1, 7, PIPE_LIGHT)
        else:
            rect(d, LOG - 2, 0, LOG - 1, 7, PIPE_DARK)

    rect(d, body_x0, 8 if top else 0, body_x1, LOG - 1, PIPE)
    if left:
        rect(d, body_x0, 8 if top else 0, body_x0 + 2, LOG - 1, PIPE_LIGHT)
    else:
        rect(d, body_x1 - 2, 8 if top else 0, body_x1, LOG - 1, PIPE_DARK)
    return img


def t_pole():
    img, d = canvas()
    rect(d, 10, 0, 13, LOG - 1, STEEL)
    rect(d, 10, 0, 11, LOG - 1, STEEL_LIGHT)
    rect(d, 12, 0, 13, LOG - 1, STEEL_DARK)
    for y in (3, 11, 19):
        rect(d, 10, y, 13, y, STEEL_DARK)
    return img


def t_flag():
    img, d = canvas()
    rect(d, 10, 2, 13, LOG - 1, STEEL)
    rect(d, 10, 2, 11, LOG - 1, STEEL_LIGHT)
    disc(d, 11, 2, 2, GOLD_LIGHT)
    rect(d, 2, 3, 10, 16, RED)
    rect(d, 2, 3, 10, 4, RED_LIGHT)
    rect(d, 2, 15, 10, 16, (176, 36, 40))
    d.polygon([(2, 8), (0, 10), (2, 12)], fill=RED)
    rect(d, 6, 6, 7, 13, GOLD_LIGHT)
    rect(d, 5, 8, 8, 9, GOLD_LIGHT)
    return img


def cloud(big):
    img, d = canvas()
    if big:
        disc(d, 7, 9, 6, WHITE)
        disc(d, 14, 7, 7, WHITE)
        disc(d, 19, 10, 5, WHITE)
        rect(d, 5, 10, 21, 15, WHITE)
        rect(d, 5, 14, 21, 15, SHADE)
        rect(d, 8, 12, 18, 13, SHADE)
    else:
        disc(d, 8, 11, 5, WHITE)
        disc(d, 15, 9, 6, WHITE)
        rect(d, 6, 12, 19, 16, WHITE)
        rect(d, 6, 15, 19, 16, SHADE)
    return img


def bush(big):
    img, d = canvas()
    rect(d, 0, 20, LOG - 1, LOG - 1, BUSH_DARK)
    if big:
        disc(d, 6, 16, 7, BUSH)
        disc(d, 12, 13, 8, BUSH)
        disc(d, 19, 16, 6, BUSH)
        disc(d, 10, 12, 4, BUSH_LIGHT)
        rect(d, 2, 19, 22, LOG - 1, BUSH)
        rect(d, 2, LOG - 1, 22, LOG - 1, BUSH_DARK)
    else:
        disc(d, 8, 16, 6, BUSH)
        disc(d, 16, 15, 7, BUSH)
        disc(d, 12, 14, 3, BUSH_LIGHT)
        rect(d, 4, 19, 20, LOG - 1, BUSH)
        rect(d, 4, LOG - 1, 20, LOG - 1, BUSH_DARK)
    return img


def sign():
    img, d = canvas()
    rect(d, 10, 12, 13, LOG - 1, WOOD)
    rect(d, 10, 12, 11, LOG - 1, WOOD_LIGHT)
    rect(d, 3, 5, 20, 15, WOOD_LIGHT)
    rect(d, 3, 5, 20, 6, (222, 180, 130))
    rect(d, 3, 14, 20, 15, WOOD_DARK)
    rect(d, 3, 5, 3, 15, WOOD_DARK)
    rect(d, 20, 5, 20, 15, WOOD_DARK)
    rect(d, 7, 12, 16, 12, WOOD_DARK)
    rect(d, 13, 9, 16, 12, WOOD_DARK)
    rect(d, 9, 9, 14, 10, WOOD_DARK)
    return img


def heart():
    img, d = canvas()
    disc(d, 9, 10, 4, RED)
    disc(d, 15, 10, 4, RED)
    d.polygon([(5, 11), (19, 11), (12, 20)], fill=RED)
    rect(d, 6, 7, 8, 8, RED_LIGHT)
    disc(d, 8, 8, 1, RED_LIGHT)
    return img


def sparkle():
    img, d = canvas()
    rect(d, 11, 2, 12, 21, GOLD_LIGHT)
    rect(d, 2, 11, 21, 12, GOLD_LIGHT)
    disc(d, 11, 11, 3, WHITE)
    rect(d, 9, 9, 14, 14, GOLD_LIGHT)
    return img


# ------------------------------------------------------------------ hero
def hero(pose, face):
    """Small explorer: yellow cap, teal jacket, blue trousers, brown boots."""
    img, d = canvas()
    l = 1 if pose in ("run2", "jump", "hurt") else 0

    # cap
    rect(d, 7, 3 + l, 15, 4 + l, CAP)
    rect(d, 6, 5 + l, 15, 6 + l, CAP)
    rect(d, 15, 6 + l, 19, 7 + l, CAP)
    rect(d, 6, 5 + l, 7, 6 + l, CAP_DARK)
    rect(d, 15, 6 + l, 18, 6 + l, CAP_DARK)

    # face
    rect(d, 8, 7 + l, 15, 12 + l, SKIN)
    rect(d, 8, 12 + l, 14, 13 + l, SKIN_DARK)
    rect(d, 7, 7 + l, 8, 11 + l, HAIR)
    rect(d, 15, 9 + l, 16, 10 + l, SKIN_DARK)
    rect(d, 13, 8 + l, 14, 9 + l, BLACK)
    rect(d, 12, 7 + l, 14, 7 + l, HAIR)
    if pose == "hurt":
        rect(d, 10, 11 + l, 13, 13 + l, (150, 40, 48))

    # torso with overalls
    top = 13 + l
    rect(d, 7, top, 15, top + 3, SHIRT)
    rect(d, 7, top, 8, top + 3, SHIRT_DARK)
    rect(d, 8, top + 2, 16, top + 6, PANTS)
    rect(d, 8, top + 5, 16, top + 6, PANTS_DARK)
    rect(d, 9, top, 10, top + 2, PANTS)
    rect(d, 13, top, 14, top + 2, PANTS)
    rect(d, 15, top + 1, 16, top + 4, SKIN)

    if pose in ("run1", "run3"):
        front = pose == "run1"
        rect(d, 8, top + 6, 10 if front else 9, top + 8, PANTS)
        rect(d, 8 if front else 7, top + 8, 11 if front else 9, top + 9, BOOT)
        rect(d, 12 if front else 13, top + 6, 14 if front else 14, top + 8, PANTS)
        rect(d, 12 if front else 14, top + 8, 15 if front else 17, top + 9, BOOT)
        if front:
            rect(d, 16, top, 17, top + 3, SHIRT)
            rect(d, 16, top + 3, 17, top + 4, SKIN)
            rect(d, 5, top + 1, 6, top + 4, SHIRT)
        else:
            rect(d, 5, top, 6, top + 3, SHIRT)
            rect(d, 4, top + 3, 5, top + 4, SKIN)
            rect(d, 16, top + 1, 17, top + 4, SHIRT)
    elif pose == "run2":
        rect(d, 9, top + 6, 10, top + 8, PANTS)
        rect(d, 8, top + 8, 11, top + 9, BOOT)
        rect(d, 13, top + 6, 14, top + 8, PANTS)
        rect(d, 13, top + 8, 16, top + 9, BOOT)
        rect(d, 16, top + 1, 17, top + 4, SHIRT)
        rect(d, 5, top + 1, 6, top + 4, SHIRT)
    elif pose == "jump":
        rect(d, 9, top + 6, 10, top + 7, PANTS)
        rect(d, 8, top + 7, 11, top + 8, BOOT)
        rect(d, 13, top + 6, 14, top + 7, PANTS)
        rect(d, 13, top + 7, 16, top + 8, BOOT)
        rect(d, 16, top - 1, 17, top + 2, SHIRT)
        rect(d, 17, top - 3, 18, top - 1, SKIN)
        rect(d, 5, top + 1, 6, top + 4, SHIRT)
    elif pose == "hurt":
        rect(d, 16, top - 2, 17, top + 1, SHIRT)
        rect(d, 18, top - 4, 19, top - 2, SKIN)
        rect(d, 4, top - 2, 5, top + 1, SHIRT)
        rect(d, 3, top - 4, 4, top - 2, SKIN)
        rect(d, 9, top + 6, 10, top + 8, PANTS)
        rect(d, 8, top + 8, 11, top + 9, BOOT)
        rect(d, 13, top + 6, 14, top + 7, PANTS)
        rect(d, 14, top + 7, 17, top + 8, BOOT)
    else:  # idle
        rect(d, 16, top, 17, top + 4, SHIRT)
        rect(d, 16, top + 4, 17, top + 5, SKIN)
        rect(d, 5, top, 6, top + 4, SHIRT)
        rect(d, 4, top + 4, 5, top + 5, SKIN)
        rect(d, 9, top + 6, 10, top + 8, PANTS)
        rect(d, 8, top + 8, 11, top + 9, BOOT)
        rect(d, 13, top + 6, 14, top + 8, PANTS)
        rect(d, 13, top + 8, 16, top + 9, BOOT)

    if face < 0:
        img = img.transpose(Image.FLIP_LEFT_RIGHT)
    return img


# ------------------------------------------------------------------ enemies
def foe(frame):
    img, d = canvas()
    lift = 1 if frame == 1 else 0
    rect(d, 4, 6 + lift, 19, 18 + lift, FOE)
    disc(d, 4, 9 + lift, 3, FOE)
    disc(d, 19, 9 + lift, 3, FOE)
    rect(d, 5, 6 + lift, 18, 7 + lift, (186, 128, 88))
    rect(d, 4, 15 + lift, 19, 18 + lift, FOE_DARK)
    # angry brows and eyes
    rect(d, 7, 9 + lift, 11, 10 + lift, FOE_DARK)
    rect(d, 12, 9 + lift, 16, 10 + lift, FOE_DARK)
    rect(d, 8, 11 + lift, 10, 13 + lift, WHITE)
    rect(d, 13, 11 + lift, 15, 13 + lift, WHITE)
    if frame == 1:
        rect(d, 9, 12 + lift, 10, 13 + lift, BLACK)
        rect(d, 14, 12 + lift, 15, 13 + lift, BLACK)
    else:
        rect(d, 8, 11 + lift, 9, 13 + lift, BLACK)
        rect(d, 13, 11 + lift, 14, 13 + lift, BLACK)
    rect(d, 9, 16 + lift, 14, 17 + lift, BLACK)
    # feet
    if frame == 1:
        rect(d, 2, 19, 9, 22, FOE_FOOT)
        rect(d, 14, 19, 21, 22, FOE_FOOT)
    else:
        rect(d, 3, 19, 10, 22, FOE_FOOT)
        rect(d, 13, 19, 20, 22, FOE_FOOT)
    return img


def foe_squash():
    img, d = canvas()
    rect(d, 2, 17, 21, 22, FOE)
    rect(d, 2, 20, 21, 22, FOE_DARK)
    rect(d, 4, 15, 19, 17, FOE)
    rect(d, 7, 18, 10, 19, BLACK)
    rect(d, 13, 18, 16, 19, BLACK)
    return img


def coin(frame):
    img, d = canvas()
    widths = (3, 6, 9, 6)
    rx = widths[frame]
    d.ellipse([12 - rx, 3, 12 + rx, 21], fill=GOLD)
    d.ellipse([12 - rx, 3, 12 + rx, 21], outline=GOLD_DARK)
    rx2 = max(1, rx - 2)
    d.ellipse([12 - rx2, 6, 12 + rx2, 18], outline=GOLD_LIGHT)
    if frame == 1:
        rect(d, 10, 6, 11, 8, WHITE)
    return img


# ------------------------------------------------------------------ sheet
CELLS = [
    ("ground_top", t_ground_top()), ("ground_fill", t_ground_fill()),
    ("brick", t_brick()), ("question_a", _qblock(2, True)),
    ("question_b", _qblock(5, True)), ("question_c", _qblock(19, False)),
    ("used", t_used()), ("platform", t_platform()),
    ("pipe_tl", pipe(True, True)), ("pipe_tr", pipe(False, True)),
    ("pipe_bl", pipe(True, False)), ("pipe_br", pipe(False, False)),
    ("pole", t_pole()), ("flag", t_flag()),
    ("cloud_big", cloud(True)), ("cloud_small", cloud(False)),
    ("bush_big", bush(True)), ("bush_small", bush(False)),
    ("sign", sign()), ("heart", heart()),
    ("hero_idle_r", hero("idle", 1)), ("hero_run1_r", hero("run1", 1)),
    ("hero_run2_r", hero("run2", 1)), ("hero_run3_r", hero("run3", 1)),
    ("hero_jump_r", hero("jump", 1)), ("hero_hurt_r", hero("hurt", 1)),
    ("hero_idle_l", hero("idle", -1)), ("hero_run1_l", hero("run1", -1)),
    ("hero_run2_l", hero("run2", -1)), ("hero_run3_l", hero("run3", -1)),
    ("hero_jump_l", hero("jump", -1)), ("hero_hurt_l", hero("hurt", -1)),
    ("foe_a", foe(0)), ("foe_b", foe(1)),
    ("foe_squash", foe_squash()), ("coin_1", coin(0)),
    ("coin_2", coin(1)), ("coin_3", coin(2)), ("coin_4", coin(3)),
    ("sparkle", sparkle()),
]


def build_sheet():
    sheet = Image.new("RGBA", (COLS * CELL, ROWS * CELL), CLEAR)
    for i, (_, cell) in enumerate(CELLS):
        big = cell.resize((CELL, CELL), Image.NEAREST)
        sheet.paste(big, ((i % COLS) * CELL, (i // COLS) * CELL), big)
    sheet.save(os.path.join(OUT, "atlas.png"))
    return sheet


# ------------------------------------------------------------------ sound
RATE = 22050


def square(freq, t, duty=0.5):
    period = RATE / freq
    return 1.0 if (t % period) / period < duty else -1.0


def tone(freq0, freq1, dur, vol=0.5, duty=0.5, decay=True):
    out = []
    n = int(RATE * dur)
    phase = 0.0
    for i in range(n):
        t = i / RATE
        freq = freq0 + (freq1 - freq0) * (i / max(1, n - 1))
        phase += freq / RATE
        value = 1.0 if (phase % 1.0) < duty else -1.0
        env = 1.0 - (i / n) * 0.85 if decay else 1.0
        out.append(value * vol * env)
    return out


def noise(dur, vol=0.4):
    import random
    random.seed(7)
    n = int(RATE * dur)
    return [random.uniform(-1, 1) * vol * (1 - i / n) for i in range(n)]


def seq(parts):
    out = []
    for p in parts:
        out.extend(p)
    return out


def save_wav(name, samples):
    path = os.path.join(OUT, name)
    with wave.open(path, "w") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        frames = b"".join(struct.pack("<h", int(max(-1.0, min(1.0, s)) * 32000)) for s in samples)
        w.writeframes(frames)


def build_sounds():
    save_wav("jump.wav", tone(330, 760, 0.13, 0.35))
    save_wav("coin.wav", seq([tone(1046, 1046, 0.055, 0.3), tone(1568, 1568, 0.1, 0.3)]))
    save_wav("stomp.wav", seq([noise(0.05, 0.35), tone(220, 90, 0.1, 0.4)]))
    save_wav("bump.wav", tone(180, 110, 0.07, 0.35, 0.25))
    save_wav("hurt.wav", tone(520, 150, 0.36, 0.4, 0.35))
    save_wav("win.wav", seq([
        tone(523, 523, 0.11, 0.3), tone(659, 659, 0.11, 0.3),
        tone(784, 784, 0.11, 0.3), tone(1047, 1047, 0.22, 0.3),
    ]))


if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    build_sheet()
    build_sounds()
    print("frames:", len(CELLS))
    for i, (name, _) in enumerate(CELLS):
        print(i, name)
