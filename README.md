# Firefight 内嵌修改菜单（非 root / 中文 / 关内即时 + 倍率）

给《交战 Firefight》（`com.windowsgames.firefight`）加一个 JMBQ 风格的游戏内悬浮菜单：
可拖动悬浮球、深色面板、Tab 页、圆点开关、倍率滑块、中文界面。

**不需要反编译 Java，不需要改 smali，不需要 root，不需要装 NDK。**

---

## 一、原理

```
原本：Java loadLibrary("SDL2") → 真·libSDL2.so → libmain.so
改后：Java loadLibrary("SDL2") → 我们的壳 libSDL2.so
                                     ├─ dlopen(libSDL2_real.so, RTLD_GLOBAL)
                                     └─ 等 libmain.so 就绪 → Dobby hook 五个点：
                                          ├─ SDL_RenderPresent → 每帧画菜单（游戏的字体渲染中文）
                                          ├─ SDL_WaitEvent / PollEvent → 截触摸
                                          ├─ Game::isArmourPenetratedByShot → 无敌 / 必穿（即时）
                                          ├─ SquadGround::hasAmmoToDoFireOrder → 无限弹药（即时）
                                          └─ SDL_RWFromFile → 改写 Data/*.txt（倍率，下一局生效）
```

- `libmain.so` 未 strip（3247 个符号），`Shot::getShooterSide()` 可直接调用 →
  判断"这发子弹是谁打的"**不需要任何结构体偏移**
- 中文用的是**游戏自己的字体** `assets/Images/Fonts/gtw.ttf`（SDL_RWFromFile 读入 +
  TTF_OpenFontRW + TTF_RenderUTF8_Blended），不额外带字体文件
- 倍率系统 hook `SDL_RWFromFile`：游戏读 `Data/**.txt` 时把文本按倍率改写后再交给它，
  游戏下一局重新解析数据时生效——这就是"下一局开局生效"的实现，不碰内存结构

---

## 二点五、"云编译"到底是什么（大白话）

**你本机没装 Android NDK（编译 ARM64 代码的工具链，装起来好几个 GB）。
云编译 = 借 GitHub 免费提供的一台 Linux 电脑来编译。**

```
你的电脑                          GitHub 的服务器（免费）
─────────                        ──────────────────────
写好源码 ──git push──▶  1. 自动装好 Android NDK
                       2. 自动拉 Dobby（hook 框架）
                       3. 自动执行 cmake 编译 arm64
                       4. 产物打包成 artifact
           ◀──点一下下载──  得到 libSDL2.so
```

你要做的只有三件事：新建空仓库 → `git push` → 去 Actions 页面下载产物。
编译在人家机器上跑，你电脑什么都不用装。

对应到本项目：`.github/workflows/build.yml` 就是"干活清单"，
写着"装 NDK → 拉 Dobby → cmake arm64 → 上传产物"，GitHub 照着单子执行。

## 二、三步走

### 1. 云编译壳库

GitHub 新建空仓库，把本目录推上去（Actions 会自动跑，含拉取 Dobby + NDK r26d + arm64 编译）：

```bash
cd firefight-modmenu
git init && git add . && git commit -m init
git branch -M main
git remote add origin https://github.com/<你的用户名>/firefight-modmenu.git
git push -u origin main
```

Actions 跑完 → 下载 artifact `libSDL2-shim-arm64` → 得到 `libSDL2.so`。

### 2. 本地打进 APK

```bash
python tools/patch_apk.py "D:\.idea\Firefight_德军增强版.zip" libSDL2.so
```

输出 `..._modmenu_unsigned.apk`（旧签名已删）。

### 3. 签名 + 安装

用你原来的签名工具签（**必须带 v2 签名**），安装。游戏里点右上角悬浮球 `M` 打开菜单。

---

## 三、菜单功能

**「即时」页** —— 勾选立即作用于当前战斗：

| 开关 | 说明 |
|---|---|
| 无敌模式（我方） | 敌方炮弹永远打不穿我方装甲 |
| 必击穿（我方） | 我方任何射击必定击穿 |
| 无限弹药（我方） | 我方小队永远不会打空 |
| 显示实时信息 | 屏幕角落显示 MOD 状态 |

**「倍率」页** —— 滑块调倍率，**下一局开局生效**：

| 滑块 | 作用 | 改写的数据 |
|---|---|---|
| 装甲倍率 x0.1~20 | 全部装甲面厚度 | `<armour>` 内 `数值@角度` |
| 携弹倍率 x1~50 | 单位载弹量 | `<rounds>` |
| 射速倍率 x0.1~10 | 武器射速 | `<rof>` |
| 数量倍率 x1~20 | 可用部队数量 | `<number>` |

「仅增强德军阵营」开关：开启后只改 `German-*` 文件和 `<countries>GERMANY` 的武器
（你的德军增强包默认开）。

**「关于」页**：使用说明。

悬浮球可以拖到任意位置；面板上滑块按下即拖动；落在面板上的触摸不会传给游戏。

---

## 四、排错

```bash
adb logcat -s FFMOD
```

| 现象 | 原因 | 处理 |
|---|---|---|
| 停在"shim 启动" | 找不到 libSDL2_real.so | 确认第 2 步跑成功 |
| 菜单出来但是方块/没字 | gtw.ttf 加载失败或缺字 | 看 logcat 的 `textInit`；缺字换个说法即可 |
| 倍率不生效 | 倍率是"下一局生效" | 重开当前关或下一关 |
| RTLD_GLOBAL 不生效、libmain 链接失败 | linker 严格按 DT_NEEDED | 备用：patchelf `--add-needed libmodmenu.so libmain.so`，壳库改名 libmodmenu.so 直接放进去（不改 SDL2） |
| 菜单点不动 | 触摸事件不是 FINGER 类型 | 部分 ROM 可能发鼠标事件，hook 里补 SDL_MOUSEBUTTONDOWN 分支 |

---

## 五、加新开关

符号表在 `libmain_symbols.txt`。挑名字带 `is/has/can/get` 的只读判定 hook 改返回值最安全；
mangled 名里 `P4Shot` = `Shot*`、`i` = int、`f` = float，参数类型可以反推；
成员函数第一个参数永远是 `this`。加一个开关三步：

1. `src/game.h`：加符号名常量 + 函数指针类型
2. `src/hooks.cpp`：加 hook 函数 + `hookSym(...)` 一行
3. `src/ui.cpp`：`instantToggles[]` 加一行

想做"改数值"级别的（装甲改 650 那种内存级修改），走 `Game::getPointerToPieceSide_I` 遍历 +
`Vehicle::getPointerToHullArmour`，但 `ARMOUR` 布局需要真机用 GameGuardian 验证一次偏移。

---

## 六、提醒

- 单机自用没事，**别用于联机**
- 游戏自带官方 Mod 目录 `/storage/emulated/0/Download/Firefight/Mod/`，改"内容"（加单位、改剧本）优先走那里
- 游戏大版本更新后符号可能变，用 `analyze/elf_probe.py` 重新核对
