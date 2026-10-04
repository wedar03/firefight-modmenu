#pragma once
// 从 libmain.so 提取的真实符号（Itanium C++ ABI mangled name）
// 全部通过 dlsym 取得地址，不链接、不需要头文件。
// 约定：非静态成员函数第一个参数是 this 指针（ARM64 走 X0）

namespace sym {

// ---- 阵营 / 判定 ----
// bool Game::isArmourPenetratedByShot_...(Shot*, const ARMOUR*, int highOrLow, int face,
//                                         float glancingAngleRadians, float* penPercentage,
//                                         float* penAtHeadOn, float* overPenMM)
inline constexpr const char* kIsArmourPenetrated =
    "_ZN4Game148isArmourPenetratedByShot_Armour_HighOrLow_Face_GlancingAngleRadians_"
    "PenetrationPercentage_PenetrationPercentageAtHeadOn_OverPenetrationInMillimetres"
    "EP4ShotPK6ARMOURiifPfS5_S5_";

// bool Game::isArmourPenetratedByShot_...(带炮盾版本)
inline constexpr const char* kIsArmourPenetratedMantlet =
    "_ZN4Game170isArmourPenetratedByShot_Armour_MantletArmour_HighOrLow_Face_Mantlet_"
    "GlancingAngleRadians_PenetrationPercentage_PenetrationPercentageAtHeadOn_"
    "OverPenetrationInMillimetresEP4ShotPK6ARMOURsiibfPfS5_S5_";

// unsigned char Shot::getShooterSide()
inline constexpr const char* kShotGetShooterSide = "_ZN4Shot14getShooterSideEv";

// unsigned char Game::getSidePlayingAs()
inline constexpr const char* kGetSidePlayingAs = "_ZN4Game16getSidePlayingAsEv";

// unsigned char Game::getSelectedSide()
inline constexpr const char* kGetSelectedSide = "_ZN4Game15getSelectedSideEv";

// bool SquadGround::hasAmmoToDoFireOrder()
inline constexpr const char* kHasAmmo = "_ZN11SquadGround20hasAmmoToDoFireOrderEv";

// unsigned char Squad::getSide()
inline constexpr const char* kSquadGetSide = "_ZN5Squad7getSideEv";

// unsigned char Piece::getSide()
inline constexpr const char* kPieceGetSide = "_ZN5Piece7getSideEv";

// int Game::getNumberPiecesInUseSide(unsigned char side)
inline constexpr const char* kGetNumberPieces = "_ZN4Game24getNumberPiecesInUseSideEh";

// int Game::getFrameNumber()
inline constexpr const char* kGetFrameNumber = "_ZN4Game14getFrameNumberEv";

// bool Game::isGameOver()
inline constexpr const char* kIsGameOver = "_ZN4Game10isGameOverEv";

// ---- 函数指针类型 ----
// 返回类型 ARM64 一律走 W0/X0，声明成 int 再自行截断最安全
using FnIsArmourPenetrated = int (*)(void* self, void* shot, const void* armour,
                                     int highOrLow, int face, float glancing,
                                     float* penPct, float* penHeadOn, float* overPenMM);
using FnIsArmourPenetratedMantlet = int (*)(void* self, void* shot, const void* armour,
                                            const void* armourMantlet, short a, short b,
                                            bool mantlet, float glancing,
                                            float* penPct, float* penHeadOn, float* overPenMM);
using FnGetU8 = unsigned char (*)(void* self);
using FnGetInt = int (*)(void* self);
using FnHasAmmo = int (*)(void* self);

}  // namespace sym

// ---- SDL2 最小声明（不引入 SDL 头文件，全部 dlsym）----
using FnRenderPresent = void (*)(void* renderer);
using FnWaitEvent = int (*)(void* event);
using FnPollEvent = int (*)(void* event);
using FnGetRendererOutputSize = int (*)(void* renderer, int* w, int* h);
using FnSetRenderDrawColor = int (*)(void* renderer, unsigned char r, unsigned char g,
                                     unsigned char b, unsigned char a);
using FnRenderFillRect = int (*)(void* renderer, const void* rect);
using FnRenderDrawRect = int (*)(void* renderer, const void* rect);
using FnGetTicks = unsigned int (*)();

// RWops / 纹理 / TTF（中文 UI 与倍率改写用）
using FnRWFromFile = void* (*)(const char* file, const char* mode);
using FnRWsize = long long (*)(void* ctx);
using FnRWseek = long long (*)(void* ctx, long long offset, int whence);
using FnRWread = unsigned long (*)(void* ctx, void* ptr, unsigned long size, unsigned long maxnum);
using FnRWclose = int (*)(void* ctx);
using FnRWFromMem = void* (*)(void* mem, int size);
using FnFreeRW = void (*)(void* ctx);
using FnTTFInit = int (*)();
using FnTTFOpenFontRW = void* (*)(void* src, int freesrc, int ptsize);
using FnTTFCloseFont = void (*)(void* font);
using FnTTFRenderUTF8Blended = void* (*)(void* font, const char* text, void* color);
using FnTTFFontHeight = int (*)(void* font);
using FnCreateTextureFromSurface = void* (*)(void* renderer, void* surface);
using FnDestroyTexture = void (*)(void* texture);
using FnFreeSurface = void (*)(void* surface);
using FnQueryTexture = int (*)(void* texture, unsigned* format, int* access, int* w, int* h);
using FnRenderCopy = int (*)(void* renderer, void* texture, const void* srcrect,
                             const void* dstrect);
using FnSetTextureBlendMode = int (*)(void* texture, int blendMode);

struct SDLColor {
    unsigned char r, g, b, a;
};

enum { SDL_BLENDMODE_BLEND = 1, SDL_RW_SEEK_SET = 0 };

struct SDLRect {
    int x, y, w, h;
};

// ---- 从 libSDL2_real.so dlsym 得到的 SDL 函数表 ----
struct SDLApi {
    FnRenderPresent RenderPresent = nullptr;
    FnWaitEvent WaitEvent = nullptr;
    FnPollEvent PollEvent = nullptr;
    FnGetRendererOutputSize GetRendererOutputSize = nullptr;
    FnSetRenderDrawColor SetRenderDrawColor = nullptr;
    FnRenderFillRect RenderFillRect = nullptr;
    FnRenderDrawRect RenderDrawRect = nullptr;
    FnGetTicks GetTicks = nullptr;
};

// 定义在 ui.cpp，hooks.cpp 负责填充
extern SDLApi g_sdl;

// SDL_TouchFingerEvent 布局（SDL2 2.x）
struct SDLFingerEvent {
    unsigned int type;
    unsigned int timestamp;
    long long touchId;
    long long fingerId;
    float x;  // 0..1 归一化
    float y;
    float dx;
    float dy;
    float pressure;
};

enum : unsigned int {
    SDL_FINGERDOWN = 0x700,
    SDL_FINGERUP = 0x701,
    SDL_FINGERMOTION = 0x702,
};
