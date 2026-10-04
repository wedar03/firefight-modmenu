#include "hooks.h"
#include "modcfg.h"
#include "text.h"
#include "ui.h"
#include "game.h"
#include <android/log.h>
#include <dlfcn.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <map>
#include <string>

#include "gothook.h"

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "FFMOD", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "FFMOD", __VA_ARGS__)

namespace {

// ---- 原函数蹦床 ----
sym::FnIsArmourPenetrated origPen = nullptr;
sym::FnIsArmourPenetratedMantlet origPenMantlet = nullptr;
sym::FnHasAmmo origHasAmmo = nullptr;
FnRenderPresent origPresent = nullptr;
FnWaitEvent origWaitEvent = nullptr;
FnPollEvent origPollEvent = nullptr;
FnRWFromFile origRWFromFile = nullptr;

// 游戏侧辅助函数
sym::FnGetU8 fnShotSide = nullptr;
sym::FnGetU8 fnSidePlayingAs = nullptr;
sym::FnGetU8 fnSelectedSide = nullptr;
sym::FnGetU8 fnSquadSide = nullptr;
sym::FnGetInt fnNumPieces = nullptr;
sym::FnGetInt fnFrameNumber = nullptr;

void* g_game = nullptr;
int g_lastW = 0, g_lastH = 0;

// RW 辅助（倍率改写用）
FnRWsize sdl_RWsize = nullptr;
FnRWseek sdl_RWseek = nullptr;
FnRWread sdl_RWread = nullptr;
FnRWclose sdl_RWclose = nullptr;
FnRWFromMem sdl_RWFromMem = nullptr;

unsigned char mySide() {
    if (g_game && fnSidePlayingAs) return fnSidePlayingAs(g_game);
    if (g_game && fnSelectedSide) return fnSelectedSide(g_game);
    return 0xFF;
}

// ---- 穿甲判定：GOD MODE / ONE HIT KILL ----
int hookIsArmourPenetrated(void* self, void* shot, const void* armour, int highOrLow, int face,
                           float glancing, float* penPct, float* penHeadOn, float* overPenMM) {
    g_game = self;
    int res = origPen ? origPen(self, shot, armour, highOrLow, face, glancing, penPct, penHeadOn,
                                overPenMM)
                      : 0;
    if (!shot || !fnShotSide) return res;
    unsigned char shooter = fnShotSide(shot);
    unsigned char mine = mySide();
    if (shooter == mine) {
        if (g_cfg.oneHitKill) return 1;
    } else {
        if (g_cfg.godMode) return 0;
    }
    return res;
}

int hookIsArmourPenetratedMantlet(void* self, void* shot, const void* armour,
                                  const void* armourMantlet, short a, short b, bool mantlet,
                                  float glancing, float* penPct, float* penHeadOn,
                                  float* overPenMM) {
    g_game = self;
    int res = origPenMantlet ? origPenMantlet(self, shot, armour, armourMantlet, a, b, mantlet,
                                              glancing, penPct, penHeadOn, overPenMM)
                             : 0;
    if (!shot || !fnShotSide) return res;
    unsigned char shooter = fnShotSide(shot);
    unsigned char mine = mySide();
    if (shooter == mine) {
        if (g_cfg.oneHitKill) return 1;
    } else {
        if (g_cfg.godMode) return 0;
    }
    return res;
}

// ---- 无限弹药 ----
int hookHasAmmo(void* self) {
    int res = origHasAmmo ? origHasAmmo(self) : 0;
    if (g_cfg.infAmmo && fnSquadSide && self) {
        unsigned char side = fnSquadSide(self);
        if (side == mySide()) return 1;
    }
    return res;
}

// ---- 渲染 ----
void hookRenderPresent(void* renderer) {
    if (renderer && g_sdl.GetRendererOutputSize) {
        int w = 0, h = 0;
        if (g_sdl.GetRendererOutputSize(renderer, &w, &h) == 0 && w > 0 && h > 0) {
            g_lastW = w;
            g_lastH = h;
            uiDraw(renderer, w, h);
        }
    }
    if (origPresent) origPresent(renderer);
}

// ---- 事件 ----
int filterEvent(void* ev) {
    if (!ev || g_lastW <= 0) return 0;
    unsigned int type = *(unsigned int*)ev;
    if (type != SDL_FINGERDOWN && type != SDL_FINGERMOTION && type != SDL_FINGERUP) return 0;
    SDLFingerEvent* f = (SDLFingerEvent*)ev;
    if (uiTouchEvent(type, f->x, f->y, g_lastW, g_lastH)) {
        *(unsigned int*)ev = 0;  // 游戏不认识 type=0，等于吞掉
        return 1;
    }
    return 0;
}

int hookWaitEvent(void* ev) {
    int ret = origWaitEvent ? origWaitEvent(ev) : 0;
    if (ret == 1) filterEvent(ev);
    return ret;
}

int hookPollEvent(void* ev) {
    int ret = origPollEvent ? origPollEvent(ev) : 0;
    if (ret == 1) filterEvent(ev);
    return ret;
}

// ---- 倍率：改写 Data/*.txt ----
void scaleTag(std::string& line, const char* tag, float mult) {
    if (mult == 1.0f) return;
    std::string open = std::string("<") + tag + ">";
    size_t p = line.find(open);
    if (p == std::string::npos) return;
    p += open.size();
    size_t e = p;
    while (e < line.size() && isdigit((unsigned char)line[e])) e++;
    if (e == p) return;
    long v = strtol(line.c_str() + p, nullptr, 10);
    long nv = (long)((float)v * mult + 0.5f);
    if (nv < 0) nv = 0;
    char buf[32];
    snprintf(buf, sizeof(buf), "%ld", nv);
    line = line.substr(0, p) + buf + line.substr(e);
}

void scaleArmourLine(std::string& line, float mult) {
    if (mult == 1.0f) return;
    size_t p = line.find('>');
    if (p == std::string::npos) return;
    p++;
    size_t e = p;
    while (e < line.size() && isdigit((unsigned char)line[e])) e++;
    if (e == p) return;
    long v = strtol(line.c_str() + p, nullptr, 10);
    long nv = (long)((float)v * mult + 0.5f);
    if (nv < 1) nv = 1;
    char buf[32];
    snprintf(buf, sizeof(buf), "%ld", nv);
    line = line.substr(0, p) + buf + line.substr(e);
}

bool g_cfg_hasMult();

std::string applyMultipliers(std::string content, const char* path) {
    bool allOne = (g_cfg.armorMult == 1.0f && g_cfg.ammoMult == 1.0f &&
                   g_cfg.rofMult == 1.0f && g_cfg.numMult == 1.0f);
    if (allOne) return content;

    bool germanFile = strstr(path, "German-") != nullptr ||
                      content.find("<countries>GERMANY") != std::string::npos;
    if (g_cfg.germanOnly && !germanFile) return content;

    std::string out;
    out.reserve(content.size() + 4096);
    bool inArmour = false;
    size_t pos = 0;
    while (pos <= content.size()) {
        size_t nl = content.find('\n', pos);
        if (nl == std::string::npos) {
            if (pos >= content.size()) break;
            nl = content.size();
        }
        std::string line = content.substr(pos, nl - pos);
        pos = nl + 1;

        if (line.find("<armour>") != std::string::npos) inArmour = true;
        if (line.find("</armour>") != std::string::npos) inArmour = false;

        scaleTag(line, "rounds", g_cfg.ammoMult);
        scaleTag(line, "number", g_cfg.numMult);
        scaleTag(line, "rof", g_cfg.rofMult);
        if (inArmour) scaleArmourLine(line, g_cfg.armorMult);

        out += line;
        out += '\n';
    }
    return out;
}

// 改写后的内容必须保活（RWFromMem 只是指向它）
std::map<std::string, std::string> g_rwCache;

void* hookRWFromFile(const char* file, const char* mode) {
    void* rw = origRWFromFile(file, mode);
    if (!rw || !file || !g_cfg_hasMult()) return rw;
    if (strncmp(file, "Data/", 5) != 0 || !strstr(file, ".txt")) return rw;

    // 读全量
    std::string content;
    char buf[8192];
    size_t n;
    while ((n = sdl_RWread(rw, buf, 1, sizeof(buf))) > 0) content.append(buf, n);
    sdl_RWclose(rw);

    auto it = g_rwCache.find(file);
    if (it == g_rwCache.end()) {
        std::string modded = applyMultipliers(std::move(content), file);
        it = g_rwCache.emplace(file, std::move(modded)).first;
        LOGI("RW hook: 改写 %s (%zu B)", file, it->second.size());
    }
    return sdl_RWFromMem((void*)it->second.data(), (int)it->second.size());
}

// 用 GOT hook：改 libmain.so 里该符号的 PLT/GOT 条目。
// origin 用 dlsym 拿到的真实地址（而不是 GOT 里可能尚未解析的 PLT 桩），避免崩溃。
template <typename T>
bool hookSym(void* handle, const char* name, void* replace, T*& origin) {
    void* addr = dlsym(handle, name);
    if (!addr) {
        LOGE("dlsym 失败: %s", name);
        return false;
    }
    void* old = nullptr;
    if (!got_hook("libmain.so", name, replace, &old)) {
        LOGE("GOT hook 失败: %s", name);
        return false;
    }
    // void* -> 函数指针：NDK 下 reinterpret_cast / 整数中转都不允许，只能 memcpy
    T fp = nullptr;
    memcpy(&fp, &addr, sizeof(addr));
    origin = fp;
    LOGI("hook 成功: %s", name);
    return true;
}

// SDL 函数：改 libmain.so 里对它们的 GOT 条目
bool hookSdl(const char* name, void* replace, void** originSlot, void* real) {
    void* old = nullptr;
    if (!got_hook("libmain.so", name, replace, &old)) {
        LOGE("SDL hook 失败: %s", name);
        return false;
    }
    if (originSlot) *originSlot = real;
    LOGI("hook 成功: %s", name);
    return true;
}

bool g_cfg_hasMult() {
    return g_cfg.armorMult != 1.0f || g_cfg.ammoMult != 1.0f || g_cfg.rofMult != 1.0f ||
           g_cfg.numMult != 1.0f;
}

}  // namespace

int ffGetMyPieceCount() {
    if (g_game && fnNumPieces) return fnNumPieces(g_game);
    return -1;
}

int ffGetFrameNumber() {
    if (g_game && fnFrameNumber) return fnFrameNumber(g_game);
    return -1;
}

int installHooks(void* mainHandle, void* sdlHandle) {
    int ok = 0;

    g_sdl.RenderPresent = reinterpret_cast<FnRenderPresent>(reinterpret_cast<uintptr_t>(dlsym(sdlHandle, "SDL_RenderPresent")));
    g_sdl.WaitEvent = reinterpret_cast<FnWaitEvent>(reinterpret_cast<uintptr_t>(dlsym(sdlHandle, "SDL_WaitEvent")));
    g_sdl.PollEvent = reinterpret_cast<FnPollEvent>(reinterpret_cast<uintptr_t>(dlsym(sdlHandle, "SDL_PollEvent")));
    g_sdl.GetRendererOutputSize =
        reinterpret_cast<FnGetRendererOutputSize>(reinterpret_cast<uintptr_t>(dlsym(sdlHandle, "SDL_GetRendererOutputSize")));
    g_sdl.SetRenderDrawColor = reinterpret_cast<FnSetRenderDrawColor>(reinterpret_cast<uintptr_t>(dlsym(sdlHandle, "SDL_SetRenderDrawColor")));
    g_sdl.RenderFillRect = reinterpret_cast<FnRenderFillRect>(reinterpret_cast<uintptr_t>(dlsym(sdlHandle, "SDL_RenderFillRect")));
    g_sdl.RenderDrawRect = reinterpret_cast<FnRenderDrawRect>(reinterpret_cast<uintptr_t>(dlsym(sdlHandle, "SDL_RenderDrawRect")));
    g_sdl.GetTicks = reinterpret_cast<FnGetTicks>(reinterpret_cast<uintptr_t>(dlsym(sdlHandle, "SDL_GetTicks")));

    sdl_RWsize = reinterpret_cast<FnRWsize>(reinterpret_cast<uintptr_t>(dlsym(sdlHandle, "SDL_RWsize")));
    sdl_RWseek = reinterpret_cast<FnRWseek>(reinterpret_cast<uintptr_t>(dlsym(sdlHandle, "SDL_RWseek")));
    sdl_RWread = reinterpret_cast<FnRWread>(reinterpret_cast<uintptr_t>(dlsym(sdlHandle, "SDL_RWread")));
    sdl_RWclose = reinterpret_cast<FnRWclose>(reinterpret_cast<uintptr_t>(dlsym(sdlHandle, "SDL_RWclose")));
    sdl_RWFromMem = reinterpret_cast<FnRWFromMem>(reinterpret_cast<uintptr_t>(dlsym(sdlHandle, "SDL_RWFromMem")));

    if (g_sdl.RenderPresent) {
        if (hookSdl("SDL_RenderPresent", (void*)hookRenderPresent, (void**)&origPresent,
                    reinterpret_cast<void*>(g_sdl.RenderPresent)))
            ok++;
    }
    if (g_sdl.WaitEvent) {
        if (hookSdl("SDL_WaitEvent", (void*)hookWaitEvent, (void**)&origWaitEvent,
                    reinterpret_cast<void*>(g_sdl.WaitEvent)))
            ok++;
    }
    if (g_sdl.PollEvent) {
        if (hookSdl("SDL_PollEvent", (void*)hookPollEvent, (void**)&origPollEvent,
                    reinterpret_cast<void*>(g_sdl.PollEvent)))
            ok++;
    }
    if (sdl_RWread && sdl_RWclose && sdl_RWFromMem) {
        origRWFromFile = reinterpret_cast<FnRWFromFile>(reinterpret_cast<uintptr_t>(dlsym(sdlHandle, "SDL_RWFromFile")));
        if (origRWFromFile) {
            if (hookSdl("SDL_RWFromFile", (void*)hookRWFromFile, (void**)&origRWFromFile,
                        reinterpret_cast<void*>(origRWFromFile)))
                ok++;
        }
    }

    if (!mainHandle) {
        LOGE("libmain.so 句柄为空，游戏侧 hook 跳过");
    } else {
        fnShotSide = reinterpret_cast<sym::FnGetU8>(reinterpret_cast<uintptr_t>(dlsym(mainHandle, sym::kShotGetShooterSide)));
        fnSidePlayingAs = reinterpret_cast<sym::FnGetU8>(reinterpret_cast<uintptr_t>(dlsym(mainHandle, sym::kGetSidePlayingAs)));
        fnSelectedSide = reinterpret_cast<sym::FnGetU8>(reinterpret_cast<uintptr_t>(dlsym(mainHandle, sym::kGetSelectedSide)));
        fnSquadSide = reinterpret_cast<sym::FnGetU8>(reinterpret_cast<uintptr_t>(dlsym(mainHandle, sym::kSquadGetSide)));
        fnNumPieces = reinterpret_cast<sym::FnGetInt>(reinterpret_cast<uintptr_t>(dlsym(mainHandle, sym::kGetNumberPieces)));
        fnFrameNumber = reinterpret_cast<sym::FnGetInt>(reinterpret_cast<uintptr_t>(dlsym(mainHandle, sym::kGetFrameNumber)));

        if (hookSym(mainHandle, sym::kIsArmourPenetrated, (void*)hookIsArmourPenetrated, origPen))
            ok++;
        if (hookSym(mainHandle, sym::kIsArmourPenetratedMantlet,
                    (void*)hookIsArmourPenetratedMantlet, origPenMantlet))
            ok++;
        if (hookSym(mainHandle, sym::kHasAmmo, (void*)hookHasAmmo, origHasAmmo)) ok++;
    }

    // 中文 UI：需要 origRWFromFile（上面刚装好）
    void* ttfHandle = dlopen("libSDL2_ttf.so", RTLD_NOLOAD);
    if (!ttfHandle) ttfHandle = dlopen("libSDL2_ttf.so", RTLD_NOW);
    if (ttfHandle && textInit(sdlHandle, ttfHandle, origRWFromFile)) {
        LOGI("中文 UI 就绪");
    } else {
        LOGE("中文 UI 初始化失败，菜单将没有文字");
    }

    LOGI("installHooks 完成，成功 %d 个", ok);
    return ok;
}
