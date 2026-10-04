#pragma once

// 自己实现的 GOT/PLT hook（不依赖 Dobby、不依赖任何第三方库）。
// 原理：把目标模块 .got.plt 里某个符号的地址换成我们的函数。
// 适用于：跨库调用（SDL_*）、以及同一 so 内走 PLT 的导出函数调用。
//
// 找不到 PLT 条目时返回 false（不崩溃，只是这项功能不生效）。

bool got_hook(const char* module_subname, const char* symbol, void* replace, void** origin);
