#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
生成 Firefight 官方 Mod 目录数据包（不需要编译、不需要重签名、不需要改 APK）。

游戏自带 Mod 机制：ModData::getModPath() = /storage/emulated/0/Download/Firefight/Mod/
游戏读 Data/**.txt 时会优先从 Mod 目录取（ModData::attemptToLoadFromFolder）。
所以我们只要把改过的文件按同样路径放进 Mod 目录，游戏就会用我们的版本。

用法：
  python make_mod_data.py "D:/.idea/Firefight_德军增强版.zip" --armor 2 --ammo 2 --rof 1.5 --num 2
  python make_mod_data.py <包> --all          # 不局限于德军，所有阵营都乘倍率
  python make_mod_data.py <包> --armor 1 --ammo 1 --rof 1 --num 1   # 原样导出（用来对照）

产出：
  <out>/Firefight/Mod/Data/**       直接推到手机 sdcard 根目录即可
  <out>/Firefight_Mod_数据包.zip     方便用 MT 管理器解压到 /sdcard/
"""
import argparse
import os
import re
import shutil
import zipfile

ARMOR_TAGS = ("upper_front", "lower_front", "upper_side", "lower_side", "side_spaced",
              "rear", "top", "bottom", "front", "side", "mantlet")


def open_apk(path):
    z = zipfile.ZipFile(path)
    name = z.namelist()[0]
    if name.lower().endswith(".apk"):
        return zipfile.ZipFile(z.open(name)), z
    return z, None


def scale_num(m):
    """把形如 >650@55 的 650 乘倍率"""
    v = int(m.group(1))
    nv = max(1, int(round(v * ARGS.armor)))
    return ">%d" % nv


def convert(text, path, german_only):
    if ARGS.armor == 1 and ARGS.ammo == 1 and ARGS.rof == 1 and ARGS.num == 1:
        return text
    is_german = ("German-" in path) or ("<countries>GERMANY" in text)
    if german_only and not is_german:
        return text

    out = []
    in_armour = False
    for line in text.split("\n"):
        s = line.strip()
        if "<armour>" in s:
            in_armour = True
        if "</armour>" in s:
            in_armour = False

        if ARGS.armor != 1 and in_armour:
            line = re.sub(r">(\d+)", scale_num, line, count=2)
        if ARGS.ammo != 1:
            line = re.sub(r"<rounds>(\d+)</rounds>",
                          lambda m: "<rounds>%d</rounds>" % max(0, int(round(int(m.group(1)) * ARGS.ammo))),
                          line)
        if ARGS.rof != 1:
            line = re.sub(r"<rof>(\d+)</rof>",
                          lambda m: "<rof>%d</rof>" % max(1, int(round(int(m.group(1)) * ARGS.rof))),
                          line)
        if ARGS.num != 1:
            line = re.sub(r"<number>(\d+)</number>",
                          lambda m: "<number>%d</number>" % max(0, int(round(int(m.group(1)) * ARGS.num))),
                          line)
        out.append(line)
    return "\n".join(out)


ARGS = None


def main():
    global ARGS
    ap = argparse.ArgumentParser()
    ap.add_argument("package", help="原始 zip 或 apk")
    ap.add_argument("--armor", type=float, default=2.0, help="装甲倍率")
    ap.add_argument("--ammo", type=float, default=2.0, help="携弹倍率")
    ap.add_argument("--rof", type=float, default=1.5, help="射速倍率")
    ap.add_argument("--num", type=float, default=2.0, help="数量倍率")
    ap.add_argument("--all", action="store_true", help="不限德军，所有阵营一起改")
    ap.add_argument("-o", "--out", default=None)
    args = ap.parse_args()
    ARGS = args

    apk, outer = open_apk(args.package)
    out = args.out or os.path.join(os.getcwd(), "Firefight_Mod_输出")
    moddir = os.path.join(out, "Firefight", "Mod")
    if os.path.isdir(moddir):
        shutil.rmtree(moddir)
    os.makedirs(moddir, exist_ok=True)

    files = [n for n in apk.namelist() if n.startswith("assets/Data/") and n.endswith(".txt")]
    print("待处理 %d 个数据文件" % len(files))
    changed = 0
    for n in files:
        rel = n[len("assets/"):]                 # Data/Vehicles/German-Tiger.txt
        data = apk.read(n).decode("utf-8", "replace")
        new = convert(data, n, not args.all)
        dst = os.path.join(moddir, *rel.split("/"))
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        with open(dst, "w", encoding="utf-8", newline="") as f:
            f.write(new)
        if new != data:
            changed += 1

    print("实际改写 %d 个文件" % changed)
    zpath = os.path.join(out, "Firefight_Mod_数据包.zip")
    if os.path.exists(zpath):
        os.remove(zpath)
    with zipfile.ZipFile(zpath, "w", zipfile.ZIP_DEFLATED) as z:
        for root, _, fs in os.walk(moddir):
            for f in fs:
                full = os.path.join(root, f)
                z.write(full, os.path.relpath(full, out))
    print("输出目录:", moddir)
    print("打包完成:", zpath, "(%.1f KB)" % (os.path.getsize(zpath) / 1024))
    print("\n用法：把 zip 里的 Firefight 文件夹解压到手机 /sdcard/ 根目录，")
    print("      也就是得到 /sdcard/Download/Firefight/Mod/Data/**，重开游戏即可。")


if __name__ == "__main__":
    main()
