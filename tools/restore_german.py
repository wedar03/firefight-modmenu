#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
把被改坏的德军数值还原成"平衡/史实"版本，输出官方 Mod 目录数据包。

三件事：
  1. 装甲 650  -> 按车型史实值（保留原倾角 @xx，只改厚度）
  2. 弹药 9999 -> 按"非德军同类武器"的统计中位数（游戏自己的平衡基准）
  3. 射速异常 -> 按史实值修正 / 封顶到非德军同类最大值

只对德军动刀，其他阵营一字不改。
"""
import argparse
import collections
import os
import re
import shutil
import statistics
import zipfile

# ============ 1. 弹药基准：非德军同类武器的 rounds 中位数（实测得出）============
AMMO_BASE = {
    "CANNON": 20, "LMG": 200, "PISTOL": 60, "RIFLE": 100, "SMG": 200,
    "GRENADE": 2, "MORTAR": 40, "RPG": 4, "FLAMETHROWER": 66, "AT": 35,
    "HMG": 750, "BOMB": 4, "RECOILLESS": 12, "MLRS": 20, "SHOTGUN": 60,
    "AT_RIFLE": 35, "GRENADE_LAUNCHER": 20, "CARBINE": 100, "SMOKE": 20,
}

# ============ 2. 射速史实值（rpm，游戏内单位）============
ROF_FIX = {
    "WEAPON_LMG_MG42": 1350, "WEAPON_LMG_MG34": 900, "WEAPON_LMG_MG34_PANZERLAUF": 900,
    "WEAPON_LMG_MG34_TWIN": 900, "WEAPON_LMG_MG13": 750, "WEAPON_LMG_MG13_SMK": 750,
    "WEAPON_LMG_MG13_TWIN": 750, "WEAPON_LMG_MG15": 1000, "WEAPON_LMG_MG17": 1100,
    "WEAPON_LMG_MG3": 1200, "WEAPON_LMG_MG4": 850, "WEAPON_LMG_MG5": 800,
    "WEAPON_LMG_MINIMI": 800, "WEAPON_LMG_MG81": 1400,
    "WEAPON_HMG_MG131": 900, "WEAPON_HMG_MG151": 700, "WEAPON_HMG_MG08": 500,
    "WEAPON_HMG_MG37": 800,
    "WEAPON_RIFLE_KAR98K": 120, "WEAPON_RIFLE_G43": 600, "WEAPON_RIFLE_FG42": 900,
    "WEAPON_RIFLE_STG44": 600, "WEAPON_RIFLE_G3": 600, "WEAPON_RIFLE_HK417": 600,
    "WEAPON_RIFLE_HK416": 850, "WEAPON_RIFLE_G36": 750, "WEAPON_RIFLE_G36C": 750,
    "WEAPON_SMG_MP40": 550, "WEAPON_SMG_MP38": 550, "WEAPON_SMG_MP5": 800,
    "WEAPON_SMG_MP7": 950, "WEAPON_PISTOL_P38": 120, "WEAPON_PISTOL_LUGER_P08": 120,
    "WEAPON_PISTOL_MAUSER_C96": 120, "WEAPON_PISTOL_HK_USP": 120,
    "WEAPON_PISTOL_GLOCK_17": 120, "WEAPON_PISTOL_GLOCK_19": 120,
}
# 非德军同类射速上限（超出就封顶，防止"比同类最强还离谱"）
ROF_CAP = {"LMG": 1800, "RIFLE": 1000, "SMG": 1100, "HMG": 900, "PISTOL": 120,
           "CANNON": 30, "AT_RIFLE": 120, "GRENADE_LAUNCHER": 300, "RPG": 60,
           "SHOTGUN": 120, "RECOILLESS_RIFLE": 4, "MLRS": 120}

# ============ 3. 装甲史实值 ============
# hull: (首上, 下前, 上侧, 下侧, 车后, 顶, 底)
# turret: (前, 侧, 后, 顶, 炮盾)
PZI = ((13, 13, 13, 13, 13, 6, 6), (13, 13, 13, 6, 13))
PZIC = ((20, 20, 14, 14, 14, 10, 6), (20, 14, 14, 10, 20))
PZII_E = ((14, 14, 14, 14, 14, 10, 10), (14, 14, 14, 10, 14))
PZII_L = ((30, 30, 20, 20, 20, 10, 10), (30, 20, 20, 10, 30))
PZ35T = ((25, 25, 15, 15, 15, 8, 8), (25, 15, 15, 8, 25))
PZIII_E = ((30, 30, 30, 30, 30, 10, 10), (30, 30, 30, 10, 30))
PZIII_J = ((50, 50, 30, 30, 30, 10, 10), (50, 30, 30, 10, 50))
PZIII_L = ((70, 70, 30, 30, 30, 10, 10), (57, 30, 30, 10, 70))
PZIV_ABC = ((15, 15, 15, 15, 15, 10, 10), (15, 15, 15, 10, 15))
PZIV_DE = ((30, 30, 20, 20, 20, 10, 10), (30, 20, 20, 10, 30))
PZIV_F = ((50, 50, 30, 30, 30, 10, 10), (50, 30, 30, 10, 50))
PZIV_H = ((80, 80, 30, 30, 30, 10, 10), (50, 30, 30, 10, 80))
PANTHER = ((80, 60, 40, 40, 40, 16, 16), (100, 45, 45, 16, 100))
PANTHER_F = ((80, 60, 40, 40, 40, 25, 16), (120, 60, 60, 25, 120))
PANTHER2 = ((100, 60, 60, 60, 60, 30, 16), (100, 60, 60, 30, 100))
TIGER = ((100, 100, 80, 80, 80, 25, 25), (100, 80, 80, 25, 100))
KT = ((150, 150, 80, 80, 80, 40, 25), (180, 80, 80, 40, 180))
MAUS = ((200, 200, 180, 180, 180, 60, 50), (220, 180, 180, 60, 220))
LOWE_S = ((150, 150, 100, 100, 100, 40, 30), (150, 100, 100, 40, 150))
LOWE_L = ((100, 100, 80, 80, 80, 30, 25), (100, 80, 80, 30, 100))
NBZ = ((20, 20, 13, 13, 13, 10, 8), (20, 13, 13, 10, 20))
HALFTRACK = ((14, 14, 8, 8, 8, 6, 6), None)
ARMORED_CAR = ((14, 14, 8, 8, 8, 6, 6), (14, 8, 8, 6, 14))
SDKFZ234 = ((30, 30, 14, 14, 14, 8, 8), (30, 14, 14, 8, 30))
LIGHT_SPG = ((15, 15, 10, 10, 10, 8, 8), (15, 10, 10, 8, 15))

# 精确匹配表：文件名(去掉 German- 前缀) -> (hull, turret)
ARMOR_TABLE = {
    "Panzer I Ausf A": PZI, "Panzer I Ausf B": PZI, "Panzer I Ausf C": PZIC,
    "Panzer I Ausf F": PZIC, "Panzer II Ausf C": PZII_E, "Panzer II Ausf D": PZII_E,
    "Panzer II Ausf F": PZII_E, "Panzer II (Flamm)": PZII_E,
    "Panzer II Ausf F (late)": PZII_L, "Panzer II Ausf J": PZII_L, "Panzer II Ausf L": PZII_L,
    "Panzer 35(t)": PZ35T, "Panzer 38(t) Ausf C": PZ35T, "Panzer 38(t) Ausf E": ((30, 30, 20, 20, 20, 10, 8), (30, 20, 20, 10, 30)),
    "Panzer III Ausf E": PZIII_E, "Panzer III Ausf F (early)": PZIII_E,
    "Panzer III Ausf F (late)": PZIII_E, "Panzer III Ausf G (early)": PZIII_E,
    "Panzer III Ausf G (late)": PZIII_E, "Panzer III Ausf H (early)": PZIII_E,
    "Panzer III Ausf H (late)": PZIII_J, "Panzer III Ausf J": PZIII_J,
    "Panzer III Ausf L": PZIII_L, "Panzer III Ausf M (early)": PZIII_L,
    "Panzer III Ausf M (late)": PZIII_L, "Panzer III Ausf N (early)": PZIII_L,
    "Panzer III Ausf N (late)": PZIII_L, "Flammpanzer III": PZIII_L,
    "Panzer IV Ausf A": PZIV_ABC, "Panzer IV Ausf B": PZIV_ABC, "Panzer IV Ausf C": PZIV_ABC,
    "Panzer IV Ausf D": PZIV_DE, "Panzer IV Ausf E": PZIV_DE,
    "Panzer IV Ausf F (early)": PZIV_DE, "Panzer IV Ausf F (late)": PZIV_F,
    "Panzer IV Ausf F2": PZIV_F, "Panzer IV Ausf G": PZIV_F,
    "Panzer IV Ausf G (early)": PZIV_F, "Panzer IV Ausf G (late)": PZIV_F,
    "Panzer IV Ausf H": PZIV_H, "Panzer IV Ausf J (early)": PZIV_H,
    "Panzer IV Ausf J (late)": PZIV_H,
    "Panther Ausf D": PANTHER, "Panther Ausf D1": PANTHER, "Panther Ausf D2": PANTHER,
    "Panther Ausf A": PANTHER, "Panther Ausf A (early)": PANTHER,
    "Panther Ausf G": PANTHER, "Panther Ausf G (early)": PANTHER,
    "Panther Ausf F": PANTHER_F, "Panther II": PANTHER2,
    "Tiger Ausf E (early)": TIGER, "Tiger Ausf E (mid)": TIGER,
    "Tiger Ausf E (late)": TIGER, "Tiger Ausf H1": TIGER,
    "King Tiger": KT, "Maus": MAUS, "Lowe Schwerer": LOWE_S, "Lowe Leichter": LOWE_L,
    "Neubaufahrzeug": NBZ,
    "Jagdtiger": ((250, 250, 80, 80, 80, 40, 25), None),
    "Jagdpanther": ((80, 60, 45, 45, 45, 16, 16), None),
    "Jagdpanther II": ((100, 60, 60, 60, 60, 30, 16), None),
    "Jagdpanzer IV": ((60, 60, 30, 30, 30, 10, 10), None),
    "Panzer IV 70(A)": ((80, 60, 30, 30, 30, 10, 10), None),
    "Panzer IV 70(V)": ((80, 60, 30, 30, 30, 10, 10), None),
    "Jagdpanzer 38 Hetzer": ((60, 60, 20, 20, 20, 8, 8), None),
    "Elefant": ((200, 200, 80, 80, 80, 30, 20), None),
    "Elefant Mk II": ((200, 200, 80, 80, 80, 30, 20), None),
    "Nashorn": ((20, 20, 10, 10, 10, 8, 8), None),
    "Marder I": LIGHT_SPG, "Marder II": LIGHT_SPG, "Marder III": LIGHT_SPG,
    "Marder III Ausf H": LIGHT_SPG, "Marder III Ausf M": LIGHT_SPG,
    "Panzerjaeger I": ((20, 20, 15, 15, 15, 8, 8), None),
    "StuG III Ausf A": ((30, 30, 20, 20, 20, 10, 10), None),
    "StuG III Ausf B": ((30, 30, 20, 20, 20, 10, 10), None),
    "StuG III Ausf C": ((50, 50, 30, 30, 30, 10, 10), None),
    "StuG III Ausf D": ((50, 50, 30, 30, 30, 10, 10), None),
    "StuG III Ausf E": ((50, 50, 30, 30, 30, 10, 10), None),
    "StuG III Ausf F": ((80, 80, 30, 30, 30, 10, 10), None),
    "StuG III Ausf F8": ((80, 80, 30, 30, 30, 10, 10), None),
    "StuG III Ausf G (early)": ((80, 80, 30, 30, 30, 10, 10), None),
    "StuG III Ausf G (late)": ((80, 80, 30, 30, 30, 10, 10), None),
    "StuG IV": ((80, 80, 30, 30, 30, 10, 10), None),
    "StuH 42": ((80, 80, 30, 30, 30, 10, 10), None),
    "StuIG 33B": ((80, 80, 30, 30, 30, 10, 10), None),
    "Grille Ausf H": LIGHT_SPG, "Grille Ausf K": LIGHT_SPG,
    "Hummel": ((10, 10, 10, 10, 10, 6, 6), None),
    "Flakpanzer I": ((13, 13, 13, 13, 13, 6, 6), (13, 13, 13, 6, 13)),
    "Flakpanzer 38(t)": ((25, 25, 15, 15, 15, 8, 8), (10, 10, 10, 0, 10)),
    "Flakpanzer IV Ostwind": ((80, 80, 30, 30, 30, 10, 10), (25, 25, 25, 0, 25)),
    "Flakpanzer IV Wirbelwind": ((80, 80, 30, 30, 30, 10, 10), (25, 25, 25, 0, 25)),
    "Flakpanzer IV Kugelblitz": ((80, 80, 30, 30, 30, 10, 10), (30, 30, 30, 10, 30)),
    "Flammpanzer 38(t)": ((50, 50, 25, 25, 25, 10, 8), None),
    "SdKfz 234-1": SDKFZ234, "SdKfz 234-2": SDKFZ234, "SdKfz 234-3": SDKFZ234,
    "SdKfz 234-4": SDKFZ234, "SdKfz 221": ARMORED_CAR, "SdKfz 221 (sPzB 41)": ARMORED_CAR,
    "SdKfz 222": ARMORED_CAR, "SdKfz 231": ARMORED_CAR, "SdKfz 231 6 Rad": ARMORED_CAR,
    "ADGZ": ((20, 20, 10, 10, 10, 8, 8), (20, 10, 10, 8, 20)),
    "SdKfz 10-4": ((8, 8, 6, 6, 6, 6, 6), None),
    "SdKfz 10-5": ((8, 8, 6, 6, 6, 6, 6), None),
}

# 关键字兜底（按优先级顺序）
FALLBACK = [
    ("SdKfz 25", HALFTRACK), ("SdKfz 250", HALFTRACK), ("Katzchen", HALFTRACK),
    ("SdKfz", ARMORED_CAR),
    ("Panzer I ", PZI), ("Panzer II", PZII_E), ("Panzer III", PZIII_E),
    ("Panzer IV", PZIV_F), ("Panther", PANTHER), ("Tiger", TIGER),
    ("Jagd", ((80, 60, 40, 40, 40, 16, 16), None)),
    ("StuG", ((80, 80, 30, 30, 30, 10, 10), None)),
    ("StuH", ((80, 80, 30, 30, 30, 10, 10), None)),
    ("Marder", LIGHT_SPG), ("Flak", ((30, 30, 20, 20, 20, 10, 10), (25, 25, 25, 0, 25))),
    ("Grille", LIGHT_SPG), ("Hummel", LIGHT_SPG),
]

# 同一语义在不同车型里字段名不同（有炮塔车用 upper_side/lower_side，固定战斗室车用 side）
HULL_FIELDS = [
    ["upper_front", "front"],
    ["lower_front"],
    ["upper_side", "side"],
    ["lower_side"],
    ["rear"],
    ["top"],
    ["bottom"],
]
TURRET_FIELDS = [["front"], ["side"], ["rear"], ["top"], ["mantlet"]]


def lookup_armor(name):
    if name in ARMOR_TABLE:
        return ARMOR_TABLE[name]
    for key, val in FALLBACK:
        if key in name:
            return val
    return None


def set_field(block, field, value):
    pattern = re.compile(r"(<%s>)(\d+)(@?-?\d*)(</%s>)" % (field, field))
    return pattern.sub(lambda m: "%s%d%s%s" % (m.group(1), value, m.group(3), m.group(4)), block)


def set_semantic(block, aliases, value):
    for a in aliases:
        if "<%s>" % a in block:
            return set_field(block, a, value)
    return block


def fix_armor(text, name):
    spec = lookup_armor(name)
    if not spec:
        return text
    hull, turret = spec
    blocks = list(re.finditer(r"<armour>.*?</armour>", text, re.S))
    if not blocks:
        return text
    out = text
    # 倒序替换，避免偏移错乱
    for idx in range(len(blocks) - 1, -1, -1):
        m = blocks[idx]
        block = m.group(0)
        spec_vals, fields = (hull, HULL_FIELDS) if idx == 0 else (turret, TURRET_FIELDS)
        if spec_vals is None:
            # 无炮塔车型：第二块（上层结构/固定战斗室）沿用车体厚度
            spec_vals = (hull[0], hull[2], hull[4], hull[5], hull[0])
            fields = TURRET_FIELDS
        # 无炮塔车型的块里字段更少，用别名兜底即可
        new = block
        for aliases, v in zip(fields, spec_vals):
            new = set_semantic(new, aliases, v)
        out = out[:m.start()] + new + out[m.end():]
    return cleanup_leftover(out, hull)


# 兜底：某些车型有 3 个以上装甲块（裙板/附加装甲），字段名也不统一。
# 任何仍是 650 的装甲字段，一律按语义回落到车体对应厚度。
def cleanup_leftover(text, hull):
    fb = {"upper_front": hull[0], "front": hull[0], "lower_front": hull[1],
          "upper_side": hull[2], "side": hull[2], "lower_side": hull[3],
          "rear": hull[4], "top": hull[5], "bottom": hull[6], "mantlet": hull[0]}

    def r(m):
        field, v = m.group(1), int(m.group(2))
        if v != 650:
            return m.group(0)
        return "<%s>%d%s</%s>" % (field, fb.get(field, hull[0]), m.group(3), field)

    return re.sub(r"<(upper_front|lower_front|upper_side|lower_side|side|rear|top|bottom|front|"
                  r"mantlet)>(\d+)(@?-?\d*)</\1>", r, text)


def fix_ammo(text):
    def repl(m):
        w, flavour = m.group(1), m.group(2) or ""
        r = int(m.group(3))
        if r != 9999:
            return m.group(0)
        mm = re.match(r"WEAPON_([A-Z0-9_]+?)_", w + "_")
        cls = mm.group(1) if mm else w
        base = AMMO_BASE.get(cls)
        if base is None:
            for k, v in AMMO_BASE.items():
                if k in w:
                    base = v
                    break
        if base is None:
            return m.group(0)
        return "<for>%s</for>%s<rounds>%d</rounds>" % (w, flavour, base)
    return re.sub(r"<for>(WEAPON_[A-Z0-9_]+)</for>(<flavour>[A-Z_]+</flavour>)?<rounds>(\d+)</rounds>",
                  repl, text)


def fix_rof(text, weapon_name):
    key = weapon_name[:-4] if weapon_name.endswith(".txt") else weapon_name
    if key.startswith("WEAPON_") and key in ROF_FIX:
        return re.sub(r"<rof>\d+</rof>", "<rof>%d</rof>" % ROF_FIX[key], text)
    m = re.search(r"<type>(WEAPON_\w+)</type>", text)
    if m:
        cls = m.group(1).replace("WEAPON_", "")
        cap = ROF_CAP.get(cls)
        if cap:
            def r2(mm):
                v = int(mm.group(1))
                return "<rof>%d</rof>" % min(v, cap)
            return re.sub(r"<rof>(\d+)</rof>", r2, text)
    return text


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("package")
    ap.add_argument("-o", "--out", default=None)
    args = ap.parse_args()

    z = zipfile.ZipFile(args.package)
    first = z.namelist()[0]
    apk = zipfile.ZipFile(z.open(first)) if first.lower().endswith(".apk") else z

    out = args.out or os.path.join(os.getcwd(), "Firefight_平衡版_输出")
    moddir = os.path.join(out, "Firefight", "Mod", "Data")
    if os.path.isdir(os.path.join(out, "Firefight")):
        shutil.rmtree(os.path.join(out, "Firefight"))
    os.makedirs(moddir, exist_ok=True)

    files = [n for n in apk.namelist() if n.startswith("assets/Data/") and n.endswith(".txt")]
    stat = collections.Counter()
    for n in files:
        rel = n[len("assets/Data/"):]
        base = os.path.basename(n)[:-4]
        text = apk.read(n).decode("utf-8", "replace")
        orig = text
        is_german_vehicle = "/Vehicles/German-" in n
        is_german_unit = "German-" in base
        is_german_weapon = bool(re.search(r"<countries>[^<]*GERMANY", text))

        if is_german_vehicle:
            new = fix_armor(text, base[len("German-"):])
            if new != text:
                stat["装甲还原"] += 1
            text = new
        if is_german_unit:
            new = fix_ammo(text)
            if new != text:
                stat["弹药还原"] += 1
            text = new
        if is_german_weapon and "/Weapons/" in n:
            new = fix_rof(text, base)
            if new != text:
                stat["射速还原"] += 1
            text = new

        if text != orig:
            dst = os.path.join(moddir, *rel.split("/"))
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            with open(dst, "w", encoding="utf-8", newline="") as f:
                f.write(text)
            stat["写入文件"] += 1

    zpath = os.path.join(out, "Firefight_平衡版数据包.zip")
    if os.path.exists(zpath):
        os.remove(zpath)
    with zipfile.ZipFile(zpath, "w", zipfile.ZIP_DEFLATED) as zz:
        for root, _, fs in os.walk(os.path.join(out, "Firefight")):
            for f in fs:
                full = os.path.join(root, f)
                zz.write(full, os.path.relpath(full, out))

    print("处理完成：", dict(stat))
    print("输出目录:", moddir)
    print("数据包:", zpath, "(%.1f KB)" % (os.path.getsize(zpath) / 1024))


if __name__ == "__main__":
    main()
