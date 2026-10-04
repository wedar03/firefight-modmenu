#pragma once
// 全局配置：弹窗里所有开关/滑块的落点。
// 即时开关由 hooks.cpp 的判定 hook 消费；倍率由 SDL_RWFromFile hook 消费（下一局生效）。

struct ModConfig {
    // ---- 即时生效 ----
    bool godMode = false;      // 敌方射击打不穿我方
    bool oneHitKill = false;   // 我方射击必击穿
    bool infAmmo = false;      // 我方小队不缺弹
    bool showInfo = false;     // 显示实时信息

    // ---- 倍率（下一局开局生效：改写 Data/*.txt 解析内容）----
    float armorMult = 1.0f;    // 装甲倍率
    float ammoMult = 1.0f;     // 携弹量倍率
    float rofMult = 1.0f;      // 射速倍率
    float numMult = 1.0f;      // 可用数量倍率
    bool germanOnly = true;    // 只增强 German（文件名/内容含 GERMANY）
};

extern ModConfig g_cfg;
