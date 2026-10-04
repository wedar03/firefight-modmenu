#include "gothook.h"

#include <android/log.h>
#include <elf.h>
#include <link.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, "FFMOD", __VA_ARGS__)

namespace {

struct FindCtx {
    const char* want;      // 模块名关键字，如 "libmain.so"
    uintptr_t base;
    const ElfW(Dyn)* dyn;
    bool found;
};

int phdr_cb(struct dl_phdr_info* info, size_t, void* data) {
    FindCtx* c = (FindCtx*)data;
    if (c->found) return 0;
    const char* n = info->dlpi_name;
    if (!n || !strstr(n, c->want)) return 0;
    for (int i = 0; i < (int)info->dlpi_phnum; i++) {
        if (info->dlpi_phdr[i].p_type == PT_DYNAMIC) {
            c->base = info->dlpi_addr;
            c->dyn = (const ElfW(Dyn)*)(info->dlpi_addr + info->dlpi_phdr[i].p_vaddr);
            c->found = true;
            return 1;
        }
    }
    return 0;
}

void* find_got_slot(uintptr_t base, const ElfW(Dyn)* dyn, const char* symbol) {
    const ElfW(Sym)* symtab = nullptr;
    const char* strtab = nullptr;
    const ElfW(Rela)* jmprel = nullptr;
    size_t pltrelsz = 0;
    const ElfW(Rela)* reladyn = nullptr;
    size_t reladynsz = 0;
    size_t syment = sizeof(ElfW(Sym));

    for (const ElfW(Dyn)* d = dyn; d->d_tag != DT_NULL; d++) {
        switch (d->d_tag) {
            case DT_SYMTAB: symtab = (const ElfW(Sym)*)(base + d->d_un.d_ptr); break;
            case DT_STRTAB: strtab = (const char*)(base + d->d_un.d_ptr); break;
            case DT_SYMENT: syment = d->d_un.d_val; break;
            case DT_JMPREL: jmprel = (const ElfW(Rela)*)(base + d->d_un.d_ptr); break;
            case DT_PLTRELSZ: pltrelsz = d->d_un.d_val; break;
            case DT_RELA: reladyn = (const ElfW(Rela)*)(base + d->d_un.d_ptr); break;
            case DT_RELASZ: reladynsz = d->d_un.d_val; break;
            default: break;
        }
    }
    if (!symtab || !strtab) return nullptr;

    // 先查 .rela.plt（函数走这里）
    if (jmprel && pltrelsz) {
        size_t n = pltrelsz / sizeof(ElfW(Rela));
        for (size_t i = 0; i < n; i++) {
            const ElfW(Rela)* r = &jmprel[i];
            uint32_t type = (uint32_t)ELF64_R_TYPE(r->r_info);
            uint32_t symidx = (uint32_t)ELF64_R_SYM(r->r_info);
            if (type != R_AARCH64_JUMP_SLOT) continue;
            if (symidx == 0) continue;
            const ElfW(Sym)* s =
                (const ElfW(Sym)*)((uintptr_t)symtab + symidx * syment);
            const char* name = strtab + s->st_name;
            if (name && strcmp(name, symbol) == 0) return (void*)(base + r->r_offset);
        }
    }
    // 再查 .rela.dyn（少数符号走 GLOB_DAT）
    if (reladyn && reladynsz) {
        size_t n = reladynsz / sizeof(ElfW(Rela));
        for (size_t i = 0; i < n; i++) {
            const ElfW(Rela)* r = &reladyn[i];
            uint32_t type = (uint32_t)ELF64_R_TYPE(r->r_info);
            uint32_t symidx = (uint32_t)ELF64_R_SYM(r->r_info);
            if (type != R_AARCH64_GLOB_DAT) continue;
            if (symidx == 0) continue;
            const ElfW(Sym)* s =
                (const ElfW(Sym)*)((uintptr_t)symtab + symidx * syment);
            const char* name = strtab + s->st_name;
            if (name && strcmp(name, symbol) == 0) return (void*)(base + r->r_offset);
        }
    }
    return nullptr;
}

}  // namespace

bool got_hook(const char* module_subname, const char* symbol, void* replace, void** origin) {
    FindCtx c{module_subname, 0, nullptr, false};
    dl_iterate_phdr(phdr_cb, &c);
    if (!c.found) {
        LOGW("got_hook: 找不到模块 %s", module_subname);
        return false;
    }
    void** slot = (void**)find_got_slot(c.base, c.dyn, symbol);
    if (!slot) {
        LOGW("got_hook: %s 里没有 %s 的 PLT 条目（可能没走 PLT，跳过）", module_subname, symbol);
        return false;
    }
    uintptr_t page = (uintptr_t)slot & ~(uintptr_t)(getpagesize() - 1);
    if (mprotect((void*)page, getpagesize() * 2, PROT_READ | PROT_WRITE) != 0) {
        LOGW("got_hook: mprotect 失败 for %s", symbol);
        return false;
    }
    if (origin) *origin = *slot;
    *slot = replace;
    mprotect((void*)page, getpagesize() * 2, PROT_READ | PROT_EXEC);
    __builtin___clear_cache((char*)slot, (char*)slot + sizeof(void*));
    return true;
}
