// 壳库：假装自己是 libSDL2.so。
// Java 层 System.loadLibrary("SDL2") 会先加载本库，本库立刻 dlopen 真正的
// libSDL2_real.so（RTLD_GLOBAL 让符号进入全局表，供后续加载的 libmain.so 解析），
// 然后开一个线程等 libmain.so 就绪，再装 hook。
#include <android/log.h>
#include <dlfcn.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "hooks.h"

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "FFMOD", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "FFMOD", __VA_ARGS__)

namespace {
void* g_realSdl = nullptr;

// 从 /proc/self/maps 找到本库所在目录（nativeLibraryDir），拼出真实 SDL2 路径
bool findOwnDir(char* out, size_t outsz) {
    FILE* f = fopen("/proc/self/maps", "re");
    if (!f) return false;
    char line[1024];
    bool found = false;
    while (fgets(line, sizeof(line), f)) {
        const char* p = strstr(line, "/libSDL2.so");
        if (!p) continue;
        const char* slash = strrchr(p, '/');
        if (!slash) continue;
        size_t len = (size_t)(slash - p) + 1;
        if (len + 1 > outsz) break;
        memcpy(out, p, len);
        out[len] = '\0';
        found = true;
        break;
    }
    fclose(f);
    return found;
}

void* waitThread(void*) {
    void* mainHandle = nullptr;
    for (int i = 0; i < 900; i++) {  // 最多等 90 秒
        mainHandle = dlopen("libmain.so", RTLD_NOLOAD);
        if (mainHandle) break;
        usleep(100 * 1000);
    }
    if (!mainHandle) {
        LOGE("等待 libmain.so 超时，只装 SDL 层 hook");
    } else {
        LOGI("libmain.so 已就绪 %p", mainHandle);
    }
    int n = installHooks(mainHandle, g_realSdl);
    LOGI("共安装 %d 个 hook", n);
    return nullptr;
}

}  // namespace

__attribute__((constructor)) static void firefightModEntry() {
    LOGI("Firefight mod shim 启动");
    char dir[512] = {0};
    if (!findOwnDir(dir, sizeof(dir))) {
        LOGE("找不到自身目录");
        return;
    }
    char realPath[640];
    snprintf(realPath, sizeof(realPath), "%slibSDL2_real.so", dir);
    g_realSdl = dlopen(realPath, RTLD_NOW | RTLD_GLOBAL);
    if (!g_realSdl) {
        LOGE("dlopen %s 失败: %s", realPath, dlerror());
        return;
    }
    LOGI("真实 SDL2 已加载: %s", realPath);

    pthread_t t;
    pthread_create(&t, nullptr, waitThread, nullptr);
    pthread_detach(t);
}
