// res_heapdef_build on Bananza (GSys only): a synthetic HeapDef table, no game data.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zstd.h>

#include "../runtime/resolution.c"

static unsigned checks, failures;
static uint64_t extra_kib;

#define CHECK(c) do { checks++; if (!(c)) { \
    failures++; fprintf(stderr, "line %d: %s\n", __LINE__, #c); \
} } while (0)

const int rt_game_version = 0;
void rt_log(const char *fmt, ...) { (void)fmt; }
uint64_t dkb_wide_heap_extra_kib(void) { return extra_kib; }

// Stubs for what the rest of resolution.c links against; none runs here.
const char *cfg_value(const char *key) { (void)key; return ""; }
int cfg_int(const char *key) { (void)key; return 0; }
int rt_patch_set(const char *name) { (void)name; return -1; }
void rt_patch_switch(int set, int on) { (void)set; (void)on; }
void *rt_patch_page(void) { return NULL; }
void fs_init(void) {}
int dkb_wide_output(unsigned out[2]) { (void)out; return 0; }
void dkb_wide_init(void) {}
GfxStats gfx_counters;

static void put32(uint8_t *p, uint32_t v) { memcpy(p, &v, 4); }

// Hash nodes {0xC1 | count << 8, count * {key index | type << 24, value}}; key table {0xC2 | count << 8, offsets, strings}.
// Keys: 0 Heaps, 1 <first heap>, 2 <second heap>, 3 Size.
static size_t build(uint8_t *b, const char *h0, const char *h1, uint32_t s0, uint32_t s1) {
    const char *keys[] = {"Heaps", h0, h1, "Size"};
    int nk = h1 ? 4 : 3;
    if (!h1) keys[2] = "Size";
    size_t keys_at = 16, strs = keys_at + 4 + 4 * (size_t)nk, at = strs;
    memset(b, 0, 512);
    memcpy(b, "YB", 2);
    b[2] = 2;
    put32(b + 4, (uint32_t)keys_at);
    put32(b + keys_at, 0xc2 | (uint32_t)nk << 8);
    for (int i = 0; i < nk; i++) {
        const char *k = i == nk - 1 ? "Size" : keys[i];
        put32(b + keys_at + 4 + 4 * i, (uint32_t)(at - keys_at));
        strcpy((char *)b + at, k);
        at += strlen(k) + 1;
    }
    size_t root = (at + 3) & ~3u, heaps = root + 12, n0 = heaps + 4 + 8 * (size_t)(h1 ? 2 : 1), n1 = n0 + 12;
    put32(b + 12, (uint32_t)root);
    put32(b + root, 0xc1 | 1u << 8);
    put32(b + root + 4, 0u | 0xc1u << 24);
    put32(b + root + 8, (uint32_t)heaps);
    put32(b + heaps, 0xc1 | (uint32_t)(h1 ? 2 : 1) << 8);
    put32(b + heaps + 4, 1u | 0xc1u << 24);
    put32(b + heaps + 8, (uint32_t)n0);
    if (h1) {
        put32(b + heaps + 12, 2u | 0xc1u << 24);
        put32(b + heaps + 16, (uint32_t)n1);
    }
    put32(b + n0, 0xc1 | 1u << 8);
    put32(b + n0 + 4, (uint32_t)(nk - 1) | 0xd1u << 24);
    put32(b + n0 + 8, s0);
    if (h1) {
        put32(b + n1, 0xc1 | 1u << 8);
        put32(b + n1 + 4, 3u | 0xd1u << 24);
        put32(b + n1 + 8, s1);
        return n1 + 12;
    }
    return n0 + 12;
}

// Decompresses the built table and reads heap `which`'s Size; -1 when the table cannot be read back.
static long sizes_after(const void *z, size_t zn, const char *h0, const char *h1, int which) {
    uint8_t out[512];
    size_t got = ZSTD_decompress(out, sizeof out, z, zn);
    if (ZSTD_isError(got)) return -1;
    Byml b = {out, got, 0};
    if (!u32_at(&b, 4, &b.keys)) return -1;
    uint32_t root, heaps;
    size_t at = u32_at(&b, 12, &root) ? field(&b, root, "Heaps", 0xc1) : 0;
    if (!at || !u32_at(&b, at, &heaps)) return -1;
    size_t s = heap_size_field(&b, heaps, which ? h1 : h0);
    uint32_t v;
    return s && u32_at(&b, s, &v) ? (long)v : -1;
}

static int run(const uint8_t *raw, size_t n, void **z, size_t *zn) {
    size_t cap = ZSTD_compressBound(n);
    void *in = malloc(cap);
    size_t in_n = ZSTD_compress(in, cap, raw, n, 3);
    int ok = res_heapdef_build(in, in_n, z, zn);
    free(in);
    return ok;
}

int main(void) {
    uint8_t raw[512];
    void *z = NULL;
    size_t zn;
    size_t n = build(raw, "GSys", "Stationed", 518144, 0x7fffffff);
    extra_kib = 0;
    CHECK(run(raw, n, &z, &zn) == 0 && z == NULL);   // nothing to add: the game's table stays
    extra_kib = 307200;
    CHECK(run(raw, n, &z, &zn) == 1);
    // GSys grows by the amount; its parent, Stationed, keeps its -1-style size and is not touched on Bananza.
    CHECK(sizes_after(z, zn, "GSys", "Stationed", 0) == 518144 + 307200);
    CHECK(sizes_after(z, zn, "GSys", "Stationed", 1) == 0x7fffffff);
    free(z);
    n = build(raw, "GSys", NULL, 518144, 0);   // GSys alone
    CHECK(run(raw, n, &z, &zn) == 1 && sizes_after(z, zn, "GSys", NULL, 0) == 518144 + 307200);
    free(z);
    n = build(raw, "Other", NULL, 518144, 0);   // no GSys: the game's table stays
    z = NULL;
    CHECK(run(raw, n, &z, &zn) == 0 && z == NULL);
    printf("res_heapdef: %u checks, %u failures\n", checks, failures);
    return failures != 0;
}
