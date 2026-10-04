#pragma once
#include "game.h"

// 用游戏自带的 gtw.ttf（assets/Images/Fonts/gtw.ttf）渲染中文菜单。
// 首次调用 installHooks 之后初始化（需要 origRWFromFile 读 assets）。

bool textInit(void* sdlHandle, void* ttfHandle, FnRWFromFile rwFromFile);
void textOnRendererChanged(void* renderer);

// 绘制 UTF-8 文本（带纹理缓存），返回像素宽；失败返回 0（缺字时 TTF 渲染为空）
int textDraw(void* renderer, const char* utf8, int x, int y, int ptsize, unsigned int abgr);
int textWidth(const char* utf8, int ptsize);
int textHeight(int ptsize);
