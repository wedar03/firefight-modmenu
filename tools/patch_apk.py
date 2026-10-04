#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
把编译好的壳库塞进 Firefight 的 APK，输出一个"待签名"的新包。

做的事：
  1. lib/arm64-v8a/libSDL2.so  -> 改名 libSDL2_real.so（真正的 SDL2）
  2. 加入我们的壳库，占住 lib/arm64-v8a/libSDL2.so 这个名字
  3. 丢掉 META-INF 里的旧签名（RSA/SF/MANIFEST.MF）
  （Python 的 zipfile 天然不保留 APK Signing Block，等于 v2/v3 签名一并清除）

用法：
  python patch_apk.py 原包.apk 壳库libSDL2.so [-o 输出.apk]

输出的是未签名包，用你平时用的工具（MT/NP 管理器、apksigner、uber-apk-signer）
签名后再安装。
"""
import argparse
import os
import shutil
import sys
import time
import zipfile

SRC_SDL2 = "lib/arm64-v8a/libSDL2.so"
DST_SDL2 = "lib/arm64-v8a/libSDL2_real.so"
SIGN_FILES = (".RSA", ".SF", "MANIFEST.MF")


def is_signature(name: str) -> bool:
    if not name.startswith("META-INF/"):
        return False
    return name.endswith(SIGN_FILES)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("apk", help="原始 APK（可以是你做好的德军增强版）")
    ap.add_argument("shim", help="GitHub Actions 编译出来的壳库 libSDL2.so")
    ap.add_argument("-o", "--out", default=None, help="输出路径")
    args = ap.parse_args()

    out = args.out or (os.path.splitext(args.apk)[0] + "_modmenu_unsigned.apk")
    shim = os.path.abspath(args.shim)
    if not os.path.isfile(shim):
        sys.exit("壳库不存在: " + shim)
    if os.path.abspath(args.apk) == os.path.abspath(out):
        sys.exit("输出路径不能和输入相同")

    src = zipfile.ZipFile(args.apk, "r")
    names = src.namelist()
    if SRC_SDL2 not in names:
        sys.exit("包里没有 " + SRC_SDL2 + "，确认这是 arm64 的 Firefight 包")

    total = len(names) + 1
    done = 0
    t0 = time.time()
    print("共 %d 个条目，开始重建（686MB 大概几分钟）..." % total)

    with zipfile.ZipFile(out, "w", allowZip64=True) as dst:
        for info in src.infolist():
            name = info.filename
            if is_signature(name):
                done += 1
                continue
            data = src.read(name)

            if name == SRC_SDL2:
                info.filename = DST_SDL2
                print("  原 SDL2 -> " + DST_SDL2)

            # 目录条目
            if name.endswith("/"):
                zi = zipfile.ZipInfo(info.filename, date_time=info.date_time)
                zi.external_attr = info.external_attr
                dst.writestr(zi, b"")
            else:
                zi = zipfile.ZipInfo(info.filename, date_time=info.date_time)
                zi.external_attr = info.external_attr
                zi.compress_type = info.compress_type
                dst.writestr(zi, data)

            done += 1
            if done % 500 == 0:
                print("  %d/%d  (%.0fs)" % (done, total, time.time() - t0))

        # 加入壳库
        zi = zipfile.ZipInfo(SRC_SDL2, date_time=time.localtime(time.time())[:6])
        zi.compress_type = zipfile.ZIP_DEFLATED
        zi.external_attr = 0o644 << 16
        with open(shim, "rb") as f:
            dst.writestr(zi, f.read())
        print("  注入壳库 -> " + SRC_SDL2)

    src.close()
    print("\n完成: %s" % out)
    print("大小: %.1f MB" % (os.path.getsize(out) / 1048576))
    print("\n下一步：用你原来的方式给这个包签名（必须 v2 签名），然后安装。")
    print("装好后屏幕右上角会出现一个圆形 [M] 按钮，点开就是修改菜单。")


if __name__ == "__main__":
    main()
