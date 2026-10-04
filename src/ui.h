#pragma once
#include "game.h"

// 每帧在 SDL_RenderPresent 之前调用
void uiDraw(void* renderer, int w, int h);

// 处理一次触摸事件；返回 true 表示要吞掉（不传给游戏）
// type: SDL_FINGERDOWN / SDL_FINGERMOTION / SDL_FINGERUP
bool uiTouchEvent(unsigned int type, float normX, float normY, int w, int h);
