#pragma once

// 在 libmain.so 与 libSDL2_real.so 都就绪后调用；返回成功安装的 hook 数
int installHooks(void* mainHandle, void* sdlHandle);

// 供 UI 显示用的实时数据
int ffGetMyPieceCount();
int ffGetFrameNumber();
