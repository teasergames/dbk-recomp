// Standalone check of runtime/mods_scan.h on a synthetic folder tree: nested files come back with '/' relative paths,
// and a missing folder reports failure.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <stdarg.h>

#include "mods_scan.h"
#include "romfs_path.h"

void rt_log(const char *fmt, ...) { (void)fmt; }

static int found, bad;
static int seen(const char *rel, const char *full, uint64_t size, void *ctx) {
    (void)ctx;
    if (!romfs_path_valid_relative(rel) || size != 3 || !strstr(full, rel)) bad++;
    if (!strcmp(rel, "a.bin") || !strcmp(rel, "x/y/b.bin") || !strcmp(rel, "x/c.bin")) found++;
    else bad++;
    return 1;
}

static void put(const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "cannot write %s\n", path); exit(2); }
    fwrite("abc", 1, 3, f);
    fclose(f);
}

int main(int argc, char **argv) {
    const char *root = argc > 1 ? argv[1] : "mods_scan_test_dir";
    char p[1024];
    snprintf(p, sizeof p, "%s/x/y/", root);
    plat_file_mkdir_p(p);
    snprintf(p, sizeof p, "%s/a.bin", root); put(p);
    snprintf(p, sizeof p, "%s/x/c.bin", root); put(p);
    snprintf(p, sizeof p, "%s/x/y/b.bin", root); put(p);
    if (!mods_walk(root, seen, NULL) || found != 3 || bad) { printf("FAIL walk found=%d bad=%d\n", found, bad); return 1; }
    snprintf(p, sizeof p, "%s/nope", root);
    if (mods_walk(p, seen, NULL)) { printf("FAIL missing folder\n"); return 1; }
    printf("mods_scan: ok\n");
    return 0;
}
