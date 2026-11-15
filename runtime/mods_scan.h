// Walks a mods folder (romfs.c, "Overlays"): calls cb(rel, full, size, ctx) for each regular file under dir, rel being
// its path below dir with '/' separators. cb returns 0 to stop. Links and (off Windows) files that are not regular, such as FIFOs, are skipped and logged; a link as dir itself is
// followed by the OS's own listing. Returns 0 when dir cannot be
// listed.
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifndef _WIN32
#include <sys/stat.h>
#endif

#include "host.h"
#include "platform/platform.h"

typedef int (*ModsFileFn)(const char *rel, const char *full, uint64_t size, void *ctx);
typedef struct { const char *root; char rel[1024]; ModsFileFn cb; void *ctx; int stop; } ModsWalk;

#ifndef _WIN32
static int plat_mods_regular(const char *path) {
    struct stat st;
    return !stat(path, &st) && S_ISREG(st.st_mode);
}
#endif

static int mods_walk_entry(const char *name, int kind, uint64_t size, void *ctx) {
    ModsWalk *w = ctx;
    size_t keep = strlen(w->rel);
    int k = snprintf(w->rel + keep, sizeof w->rel - keep, "%s%s", keep ? "/" : "", name);
    if (k < 0 || (size_t)k >= sizeof w->rel - keep) {
        w->rel[keep] = 0;
        return 1;   // too long for any RomFS path: skipped
    }
    char full[2200];
    if (snprintf(full, sizeof full, "%s/%s", w->root, w->rel) < (int)sizeof full) {
        if (kind == PLAT_FS_LINK) rt_log("RomFS mods: skipped %s (a link or placeholder, not followed)", full);
#ifndef _WIN32
        else if (kind == PLAT_FS_FILE && !plat_mods_regular(full)) rt_log("RomFS mods: skipped %s (not a regular file)", full);
#endif
        else if (kind == PLAT_FS_DIR) plat_fs_list(full, mods_walk_entry, w);
        else if (kind == PLAT_FS_FILE && !w->cb(w->rel, full, size, w->ctx)) w->stop = 1;
    }
    w->rel[keep] = 0;
    return !w->stop;
}

static inline int mods_walk(const char *dir, ModsFileFn cb, void *ctx) {
    ModsWalk w = {dir, "", cb, ctx, 0};
    return plat_fs_list(dir, mods_walk_entry, &w) == PLAT_FS_OK;
}
