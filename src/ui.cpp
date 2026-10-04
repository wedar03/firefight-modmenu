#include "ui.h"
#include "modcfg.h"
#include "text.h"
#include <math.h>
#include <stdio.h>

// 全局 SDL 函数表（hooks.cpp 里 dlsym 填好，绘制时读）
SDLApi g_sdl;

// 全局配置（modcfg.h 里声明，这里定义）
ModConfig g_cfg;

namespace {

// ---- 颜色（ABGR 打包顺序见 textDraw 的 rgba 参数：0xRRGGBBAA）----
constexpr unsigned kCPanel = 0x1B1F26E6;      // 面板底（带透明度）
constexpr unsigned kCPanelEdge = 0x3A404CFF;
constexpr unsigned kCTitle = 0xE8EAEDFF;
constexpr unsigned kCTabSel = 0xFF5A5AFF;     // 选中 Tab（红）
constexpr unsigned kCTabIdle = 0x9AA0A8FF;
constexpr unsigned kCToggleOn = 0xE24B4AFF;   // 开（红圆）
constexpr unsigned kCToggleOff = 0x5A5A5AFF;
constexpr unsigned kCSliderFill = 0xC8A04FFF; // 蓝绿
constexpr unsigned kCKnob = 0xF0F0F0FF;
constexpr unsigned kCValue = 0x90D07FFF;      // 数值（绿）
constexpr unsigned kCWarn = 0xFF9E5AFF;       // 橙黄提示
constexpr unsigned kCRowBgOn = 0x302424FF;
constexpr unsigned kCRowBg = 0x282C34FF;

constexpr int kBallR = 44;
constexpr int kPanelMaxW = 560;
constexpr int kTitleH = 58;
constexpr int kTabH = 54;
constexpr int kRowH = 70;
constexpr int kSliderRowH = 96;
constexpr int kBottomH = 58;
constexpr int kPad = 16;

struct UiState {
    bool inited = false;
    float ballX = 0, ballY = 0;   // 像素（球心）
    bool open = false;
    int tab = 0;
    bool dragBall = false;
    int dragSlider = -1;
    float downX = 0, downY = 0;
    bool downOnBall = false;
    bool moved = false;
    int W = 0, H = 0;
};
UiState g_ui;

// ---- 几何 ----
struct PanelGeo {
    int x, y, w, h, contentY;
};
PanelGeo panelGeo(int W, int H) {
    PanelGeo g;
    g.w = kPanelMaxW;
    if (g.w > W - 32) g.w = W - 32;
    if (g.w < 320) g.w = W - 24 > 320 ? W - 24 : 320;
    int rows = (g_ui.tab == 1) ? (4 * kSliderRowH + kRowH) : (4 * kRowH);
    g.h = kTitleH + kTabH + rows + kBottomH + kPad;
    if (g.h > H - 60) g.h = H - 60;
    g.x = W - g.w - 20;
    if (g.x < 12) g.x = (W - g.w) / 2;
    g.y = 110;
    if (g.y + g.h > H - 20) g.y = H - 20 - g.h;
    g.contentY = g.y + kTitleH + kTabH;
    return g;
}

void fillRect(void* r, int x, int y, int w, int h, unsigned abgr) {
    SDLRect rc{x, y, w, h};
    unsigned char a = (unsigned char)(abgr & 0xFF);
    unsigned char b = (unsigned char)((abgr >> 8) & 0xFF);
    unsigned char g = (unsigned char)((abgr >> 16) & 0xFF);
    unsigned char rr = (unsigned char)((abgr >> 24) & 0xFF);
    g_sdl.SetRenderDrawColor(r, rr, g, b, a);
    g_sdl.RenderFillRect(r, &rc);
}

void fillCircle(void* r, int cx, int cy, int rad, unsigned abgr) {
    for (int dy = -rad; dy <= rad; dy++) {
        int half = (int)sqrtf((float)(rad * rad - dy * dy));
        fillRect(r, cx - half, cy + dy, half * 2, 1, abgr);
    }
}

void circleOutline(void* r, int cx, int cy, int rad, unsigned abgr) {
    for (int dy = -rad; dy <= rad; dy++) {
        int half = (int)sqrtf((float)(rad * rad - dy * dy));
        fillRect(r, cx - half, cy + dy, 2, 1, abgr);
        fillRect(r, cx + half - 2, cy + dy, 2, 1, abgr);
    }
}

// ---- 控件绘制 ----
void drawToggle(void* rend, int cx, int cy, bool on) {
    fillCircle(rend, cx, cy, 20, on ? kCToggleOn : kCToggleOff);
    fillCircle(rend, cx + (on ? 9 : -9), cy, 13, 0xF2F2F2FF);
}

void drawSlider(void* rend, int x0, int x1, int cy, float t) {
    fillRect(rend, x0, cy - 4, x1 - x0, 8, 0x50565EFF);
    int kx = x0 + (int)((x1 - x0) * t);
    fillRect(rend, x0, cy - 4, kx - x0, 8, kCSliderFill);
    fillCircle(rend, kx, cy, 15, kCKnob);
}

float sliderT(float v, float lo, float hi) { return (v - lo) / (hi - lo); }

void fmtMult(char* out, size_t n, float v) {
    if (v >= 9.95f) snprintf(out, n, "x%.0f", v);
    else snprintf(out, n, "x%.1f", v);
}

// 倍率行定义
struct SliderRow {
    const char* label;
    float* value;
    float lo, hi;
};
SliderRow sliders[] = {
    {"装甲倍率", &g_cfg.armorMult, 0.1f, 20.0f},
    {"携弹倍率", &g_cfg.ammoMult, 1.0f, 50.0f},
    {"射速倍率", &g_cfg.rofMult, 0.1f, 10.0f},
    {"数量倍率", &g_cfg.numMult, 1.0f, 20.0f},
};
constexpr int kSliderCount = 4;

struct ToggleRow {
    const char* label;
    bool* value;
};
ToggleRow instantToggles[] = {
    {"无敌模式（我方）", &g_cfg.godMode},
    {"必击穿（我方）", &g_cfg.oneHitKill},
    {"无限弹药（我方）", &g_cfg.infAmmo},
    {"显示实时信息", &g_cfg.showInfo},
};
constexpr int kInstantCount = 4;

const char* kTabs[3] = {"即时", "倍率", "关于"};

}  // namespace

void uiDraw(void* renderer, int W, int H) {
    if (!renderer || !g_sdl.SetRenderDrawColor) return;
    g_ui.W = W;
    g_ui.H = H;
    if (!g_ui.inited) {
        g_ui.ballX = W - 90;
        g_ui.ballY = (int)(H * 0.22f);
        g_ui.inited = true;
    }

    // ---- 悬浮球 ----
    fillCircle(renderer, (int)g_ui.ballX, (int)g_ui.ballY, kBallR, 0x101318E0);
    circleOutline(renderer, (int)g_ui.ballX, (int)g_ui.ballY, kBallR, 0x5A6472E0);
    textDraw(renderer, "M", (int)g_ui.ballX - 12, (int)g_ui.ballY - 17, 32, 0xFFD84AFF);

    if (g_cfg.showInfo && !g_ui.open) {
        char buf[64];
        snprintf(buf, sizeof(buf), "MOD x%.1f", g_cfg.armorMult);
        textDraw(renderer, buf, 20, 20, 24, 0x90FF90FF);
    }

    if (!g_ui.open) return;

    PanelGeo g = panelGeo(W, H);
    fillRect(renderer, g.x, g.y, g.w, g.h, kCPanel);
    fillRect(renderer, g.x, g.y, g.w, 2, kCPanelEdge);
    fillRect(renderer, g.x, g.y + g.h - 2, g.w, 2, kCPanelEdge);

    // ---- 标题栏 ----
    textDraw(renderer, "FIREFIGHT MOD", g.x + kPad, g.y + 16, 30, kCTitle);
    textDraw(renderer, "v1.0", g.x + g.w - 86, g.y + 22, 22, kCTabIdle);

    // ---- Tab 栏 ----
    int tabW = g.w / 3;
    for (int i = 0; i < 3; i++) {
        int tx = g.x + i * tabW;
        int ty = g.y + kTitleH;
        if (g_ui.tab == i) fillRect(renderer, tx + 8, ty, tabW - 16, kTabH - 6, 0x332424FF);
        const char* t = kTabs[i];
        unsigned c = (g_ui.tab == i) ? kCTabSel : kCTabIdle;
        // 粗略居中：中文按 1 字 = ptsize 宽估
        int wpx = (int)(28 * (float)(i == 2 ? 2 : 2));
        textDraw(renderer, t, tx + (tabW - wpx) / 2, ty + 12, 28, c);
    }

    int rowX = g.x + kPad;
    int rowW = g.w - kPad * 2;
    int y = g.contentY + 6;

    if (g_ui.tab == 0) {
        for (int i = 0; i < kInstantCount; i++) {
            bool on = *instantToggles[i].value;
            fillRect(renderer, rowX, y, rowW, kRowH - 8, on ? kCRowBgOn : kCRowBg);
            textDraw(renderer, instantToggles[i].label, rowX + 14, y + 20, 26, kCTitle);
            drawToggle(renderer, rowX + rowW - 40, y + kRowH / 2 - 4, on);
            y += kRowH;
        }
    } else if (g_ui.tab == 1) {
        for (int i = 0; i < kSliderCount; i++) {
            float v = *sliders[i].value;
            char val[32];
            fmtMult(val, sizeof(val), v);
            textDraw(renderer, sliders[i].label, rowX + 14, y + 6, 26, kCTitle);
            char buf[64];
            snprintf(buf, sizeof(buf), "%s: %s", "", val);
            textDraw(renderer, val, rowX + rowW - 110, y + 6, 26, kCValue);
            drawSlider(renderer, rowX + 20, rowX + rowW - 30, y + 62,
                       sliderT(v, sliders[i].lo, sliders[i].hi));
            y += kSliderRowH;
        }
        // 仅德军
        fillRect(renderer, rowX, y, rowW, kRowH - 8, g_cfg.germanOnly ? kCRowBgOn : kCRowBg);
        textDraw(renderer, "仅增强德军阵营", rowX + 14, y + 20, 26, kCTitle);
        drawToggle(renderer, rowX + rowW - 40, y + kRowH / 2 - 4, g_cfg.germanOnly);
        y += kRowH;
        textDraw(renderer, "* 倍率改写数据文件，下一局开局生效", rowX + 8, y + 6, 22, kCWarn);
    } else {
        const char* lines[] = {
            "即时开关：勾选后当前战斗立即生效",
            "倍率：改写游戏读取的装备数据，下一局生效",
            "",
            "改判定而不是改内存，无需知道结构体偏移",
            "点击悬浮球 M 可收回面板",
        };
        for (const char* s : lines) {
            if (*s) textDraw(renderer, s, rowX + 8, y + 8, 24, kCTabIdle);
            y += 44;
        }
    }

    // ---- 底栏 ----
    int by = g.y + g.h - kBottomH;
    fillRect(renderer, g.x, by, g.w, kBottomH, 0x14171CFF);
    textDraw(renderer, "关闭窗口", g.x + g.w - 128, by + 16, 26, 0x6FB2FFFF);
}

namespace {

int hitSliderRow(int x, int y, const PanelGeo& g) {
    int rowX = g.x + kPad, rowW = g.w - kPad * 2;
    int cy = g.contentY + 6;
    for (int i = 0; i < kSliderCount; i++) {
        int trackY = cy + 62;
        if (y >= trackY - 34 && y <= trackY + 34 && x >= rowX + 6 && x <= rowX + rowW - 6)
            return i;
        cy += kSliderRowH;
    }
    return -1;
}

}  // namespace

bool uiTouchEvent(unsigned int type, float nx, float ny, int W, int H) {
    if (!g_ui.inited) return false;
    int x = (int)(nx * (float)W);
    int y = (int)(ny * (float)H);

    if (type == SDL_FINGERDOWN) {
        g_ui.downX = (float)x;
        g_ui.downY = (float)y;
        g_ui.moved = false;
        int dx = x - (int)g_ui.ballX, dy = y - (int)g_ui.ballY;
        g_ui.downOnBall = (dx * dx + dy * dy <= (kBallR + 12) * (kBallR + 12));
        if (g_ui.downOnBall) {
            g_ui.dragBall = true;
            return true;
        }
        if (!g_ui.open) return false;
        PanelGeo g = panelGeo(W, H);
        if (x < g.x || y < g.y || x > g.x + g.w || y > g.y + g.h) return false;

        // Tab
        if (y >= g.y + kTitleH && y < g.y + kTitleH + kTabH) {
            int i = (x - g.x) * 3 / g.w;
            if (i >= 0 && i < 3) g_ui.tab = i;
            return true;
        }
        // 关闭窗口
        if (y >= g.y + g.h - kBottomH) {
            if (x >= g.x + g.w - 150) g_ui.open = false;
            return true;
        }
        // 内容区
        if (g_ui.tab == 0) {
            int idx = (y - g.contentY - 6) / kRowH;
            if (idx >= 0 && idx < kInstantCount) *instantToggles[idx].value = !*instantToggles[idx].value;
            return true;
        }
        if (g_ui.tab == 1) {
            int s = hitSliderRow(x, y, g);
            if (s >= 0) {
                g_ui.dragSlider = s;
                return true;  // 拖动由 MOTION 处理
            }
            // 仅德军 toggle 行
            int ty = g.contentY + 6 + kSliderCount * kSliderRowH;
            if (y >= ty && y < ty + kRowH) g_cfg.germanOnly = !g_cfg.germanOnly;
            return true;
        }
        return true;  // 关于页整个吞掉
    }

    if (type == SDL_FINGERMOTION) {
        if (g_ui.dragBall) {
            g_ui.ballX = (float)x;
            g_ui.ballY = (float)y;
            float m = (float)(kBallR + 6);
            if (g_ui.ballX < m) g_ui.ballX = m;
            if (g_ui.ballY < m) g_ui.ballY = m;
            if (g_ui.ballX > W - m) g_ui.ballX = (float)(W - m);
            if (g_ui.ballY > H - m) g_ui.ballY = (float)(H - m);
            return true;
        }
        if (g_ui.dragSlider >= 0 && g_ui.open) {
            PanelGeo g = panelGeo(W, H);
            int rowX = g.x + kPad, rowW = g.w - kPad * 2;
            int x0 = rowX + 20, x1 = rowX + rowW - 30;
            float t = (float)(x - x0) / (float)(x1 - x0);
            if (t < 0) t = 0;
            if (t > 1) t = 1;
            SliderRow& s = sliders[g_ui.dragSlider];
            *s.value = s.lo + (s.hi - s.lo) * t;
            return true;
        }
        if (g_ui.open) {
            PanelGeo g = panelGeo(W, H);
            if (x >= g.x && x <= g.x + g.w && y >= g.y && y <= g.y + g.h) return true;
        }
        return false;
    }

    if (type == SDL_FINGERUP) {
        bool wasBall = g_ui.dragBall;
        bool wasSlider = g_ui.dragSlider >= 0;
        g_ui.dragBall = false;
        g_ui.dragSlider = -1;
        float dx = (float)x - g_ui.downX, dy = (float)y - g_ui.downY;
        bool click = (dx * dx + dy * dy < 144);  // <12px 视为点击
        if (wasBall && click && !g_ui.moved) {
            g_ui.open = !g_ui.open;
            return true;
        }
        (void)wasSlider;
        return wasBall || wasSlider;
    }
    return false;
}
