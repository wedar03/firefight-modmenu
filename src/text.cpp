#include "text.h"
#include <android/log.h>
#include <dlfcn.h>
#include <stdio.h>
#include <map>
#include <string>
#include <vector>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "FFMOD", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "FFMOD", __VA_ARGS__)

namespace {

struct TextApi {
    FnTTFInit TTF_Init = nullptr;
    FnTTFOpenFontRW TTF_OpenFontRW = nullptr;
    FnTTFRenderUTF8Blended TTF_RenderUTF8_Blended = nullptr;
    FnCreateTextureFromSurface SDL_CreateTextureFromSurface = nullptr;
    FnDestroyTexture SDL_DestroyTexture = nullptr;
    FnFreeSurface SDL_FreeSurface = nullptr;
    FnQueryTexture SDL_QueryTexture = nullptr;
    FnRenderCopy SDL_RenderCopy = nullptr;
    FnSetTextureBlendMode SDL_SetTextureBlendMode = nullptr;
    FnRWFromMem SDL_RWFromMem = nullptr;
    FnRWsize SDL_RWsize = nullptr;
    FnRWseek SDL_RWseek = nullptr;
    FnRWread SDL_RWread = nullptr;
    FnRWclose SDL_RWclose = nullptr;
    FnGetTicks SDL_GetTicks = nullptr;
};

TextApi t;
std::vector<char> g_fontData;          // gtw.ttf 全量
std::map<int, void*> g_fonts;          // ptsize -> TTF_Font*
std::map<std::string, int> g_texW;     // key -> 宽
void* g_lastRend = nullptr;

struct Tex {
    void* tex = nullptr;
    int w = 0, h = 0;
};
std::map<std::string, Tex> g_cache;

void* fontFor(int ptsize) {
    auto it = g_fonts.find(ptsize);
    if (it != g_fonts.end()) return it->second;
    if (g_fontData.empty()) return nullptr;
    void* rw = t.SDL_RWFromMem((void*)g_fontData.data(), (int)g_fontData.size());
    if (!rw) return nullptr;
    void* f = t.TTF_OpenFontRW(rw, 1, ptsize);  // freesrc=1：TTF 会收掉 RW
    g_fonts[ptsize] = f;
    return f;
}

Tex* cacheLookup(const std::string& key, const char* text, int pt, unsigned rgba, void* rend) {
    auto it = g_cache.find(key);
    if (it != g_cache.end()) return &it->second;
    Tex tx;
    if (t.TTF_RenderUTF8_Blended && t.SDL_CreateTextureFromSurface) {
        SDLColor c{(unsigned char)(rgba >> 24), (unsigned char)(rgba >> 16),
                   (unsigned char)(rgba >> 8), (unsigned char)rgba};
        void* surf = t.TTF_RenderUTF8_Blended(fontFor(pt), text, &c);
        if (surf) {
            tx.tex = t.SDL_CreateTextureFromSurface(rend, surf);
            if (tx.tex) {
                if (t.SDL_SetTextureBlendMode) t.SDL_SetTextureBlendMode(tx.tex, SDL_BLENDMODE_BLEND);
                unsigned fmt = 0;
                int acc = 0;
                t.SDL_QueryTexture(tx.tex, &fmt, &acc, &tx.w, &tx.h);
            }
            if (t.SDL_FreeSurface) t.SDL_FreeSurface(surf);
        }
    }
    if (g_cache.size() > 220) g_cache.clear();
    auto res = g_cache.emplace(key, tx);
    return &res.first->second;
}

std::string makeKey(const char* text, int pt, unsigned rgba) {
    char head[40];
    snprintf(head, sizeof(head), "%d:%u:", pt, rgba);
    return std::string(head) + text;
}

}  // namespace

bool textInit(void* sdlHandle, void* ttfHandle, FnRWFromFile rwFromFile) {
    if (!sdlHandle || !ttfHandle || !rwFromFile) return false;

    t.TTF_Init = reinterpret_cast<FnTTFInit>(reinterpret_cast<uintptr_t>(dlsym(ttfHandle, "TTF_Init")));
    t.TTF_OpenFontRW = reinterpret_cast<FnTTFOpenFontRW>(reinterpret_cast<uintptr_t>(dlsym(ttfHandle, "TTF_OpenFontRW")));
    t.TTF_RenderUTF8_Blended = reinterpret_cast<FnTTFRenderUTF8Blended>(reinterpret_cast<uintptr_t>(dlsym(ttfHandle, "TTF_RenderUTF8_Blended")));
    t.SDL_CreateTextureFromSurface =
        reinterpret_cast<FnCreateTextureFromSurface>(reinterpret_cast<uintptr_t>(dlsym(sdlHandle, "SDL_CreateTextureFromSurface")));
    t.SDL_DestroyTexture = reinterpret_cast<FnDestroyTexture>(reinterpret_cast<uintptr_t>(dlsym(sdlHandle, "SDL_DestroyTexture")));
    t.SDL_FreeSurface = reinterpret_cast<FnFreeSurface>(reinterpret_cast<uintptr_t>(dlsym(sdlHandle, "SDL_FreeSurface")));
    t.SDL_QueryTexture = reinterpret_cast<FnQueryTexture>(reinterpret_cast<uintptr_t>(dlsym(sdlHandle, "SDL_QueryTexture")));
    t.SDL_RenderCopy = reinterpret_cast<FnRenderCopy>(reinterpret_cast<uintptr_t>(dlsym(sdlHandle, "SDL_RenderCopy")));
    t.SDL_SetTextureBlendMode =
        reinterpret_cast<FnSetTextureBlendMode>(reinterpret_cast<uintptr_t>(dlsym(sdlHandle, "SDL_SetTextureBlendMode")));
    t.SDL_RWFromMem = reinterpret_cast<FnRWFromMem>(reinterpret_cast<uintptr_t>(dlsym(sdlHandle, "SDL_RWFromMem")));
    t.SDL_RWsize = reinterpret_cast<FnRWsize>(reinterpret_cast<uintptr_t>(dlsym(sdlHandle, "SDL_RWsize")));
    t.SDL_RWseek = reinterpret_cast<FnRWseek>(reinterpret_cast<uintptr_t>(dlsym(sdlHandle, "SDL_RWseek")));
    t.SDL_RWread = reinterpret_cast<FnRWread>(reinterpret_cast<uintptr_t>(dlsym(sdlHandle, "SDL_RWread")));
    t.SDL_RWclose = reinterpret_cast<FnRWclose>(reinterpret_cast<uintptr_t>(dlsym(sdlHandle, "SDL_RWclose")));
    t.SDL_GetTicks = reinterpret_cast<FnGetTicks>(reinterpret_cast<uintptr_t>(dlsym(sdlHandle, "SDL_GetTicks")));

    if (!t.TTF_OpenFontRW || !t.TTF_RenderUTF8_Blended || !t.SDL_CreateTextureFromSurface ||
        !t.SDL_RenderCopy) {
        LOGE("textInit: TTF/SDL 符号不全");
        return false;
    }
    if (t.TTF_Init) t.TTF_Init();

    // 读游戏自带中文字体
    void* rw = rwFromFile("Images/Fonts/gtw.ttf", "rb");
    if (!rw) {
        LOGE("textInit: 读不到 Images/Fonts/gtw.ttf");
        return false;
    }
    if (t.SDL_RWsize && t.SDL_RWseek) {
        long long sz = t.SDL_RWsize(rw);
        if (sz > 0 && sz < 32 * 1024 * 1024) g_fontData.resize((size_t)sz);
    }
    if (g_fontData.empty()) g_fontData.resize(1024 * 1024);
    size_t total = 0;
    for (int round = 0; round < 64; round++) {
        if (total >= g_fontData.size()) g_fontData.resize(g_fontData.size() * 2);
        size_t n = t.SDL_RWread(rw, g_fontData.data() + total, 1,
                                g_fontData.size() - total);
        if (n == 0) break;
        total += n;
    }
    t.SDL_RWclose(rw);
    g_fontData.resize(total);
    LOGI("textInit: gtw.ttf %zu 字节", total);
    return total > 1000;
}

void textOnRendererChanged(void* renderer) {
    if (g_lastRend != renderer) {
        g_cache.clear();
        g_lastRend = renderer;
    }
}

int textDraw(void* renderer, const char* utf8, int x, int y, int ptsize, unsigned int rgba) {
    if (!renderer || !utf8 || !*utf8) return 0;
    textOnRendererChanged(renderer);
    std::string key = makeKey(utf8, ptsize, rgba);
    Tex* tx = cacheLookup(key, utf8, ptsize, rgba, renderer);
    if (!tx || !tx->tex || !t.SDL_RenderCopy) return 0;
    SDLRect dst{x, y, tx->w, tx->h};
    t.SDL_RenderCopy(renderer, tx->tex, nullptr, &dst);
    return tx->w;
}

int textWidth(const char* utf8, int ptsize) {
    // 只取宽：渲染缓存但不画（需要一个 renderer 才能建纹理，这里不缓存纹理宽度也行——
    // 简化：菜单布局用固定字宽估算，见 ui.cpp）
    (void)utf8;
    (void)ptsize;
    return 0;
}

int textHeight(int ptsize) {
    return ptsize + 6;
}
