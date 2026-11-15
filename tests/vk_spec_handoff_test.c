// The helper threads' handoff (runtime/vk_spec.c), without a device: vk_spec_handoff_test.py compiles the production
// file unchanged with the real platform layer and this fixture, which stands in for the configuration, the log, the
// Vulkan context and the three Vulkan calls the file makes. The helper threads are real threads.
//
// One process per configuration, because vk_spec_on reads its knobs once:
//   vk_spec_handoff <TOTK_REPLAY_PAR or -> <TOTK_REPLAY_HELPERS or -> <helper threads expected, or cpu>
// "cpu" expects vk_spec_helpers_for of the logical processors of this process, so the default goes through it.
//   vk_spec_handoff counts
// checks vk_spec_helpers_for itself, for every number of logical processors.
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cfg.h"
#include "host.h"
#include "platform/platform.h"
#include "vk.h"
#include "vk_spec.h"

static int failures, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { failures++; printf("FAIL line %d: %s: ", __LINE__, #c); printf(__VA_ARGS__); printf("\n"); } } while (0)

// ---- what vk_spec.c reads from the rest of the runtime
static const char *par_value, *helpers_value;   // NULL: not set
static int ab_block;
static char logged[1024];

const char *cfg_get(const char *name) {
    if (!strcmp(name, "TOTK_REPLAY_PAR")) return par_value;
    if (!strcmp(name, "TOTK_REPLAY_HELPERS")) return helpers_value;
    return NULL;
}

// The rule of runtime/perf.c rt_knob: the default when the name is unset or empty, 2 for "ab", 0 for "0", 1 otherwise.
int rt_knob(const char *name, const char *def) {
    char key[64];
    snprintf(key, sizeof key, "TOTK_%s", name);
    const char *e = cfg_get(key);
    if (!e || !*e) e = def;
    return !strcmp(e, "ab") ? 2 : strcmp(e, "0") != 0;
}

int rt_ab(void) { return ab_block; }

void rt_log(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    size_t o = strlen(logged);
    vsnprintf(logged + o, sizeof logged - o, fmt, ap);
    va_end(ap);
}

static VkCtx ctx;
static int ctx_gone;
VkCtx *vk_ctx(void) { return ctx_gone ? NULL : &ctx; }

static uint64_t fence_done;
uint64_t vk_sync_fence_done(void) { return fence_done; }

// The Vulkan calls: handles are counters, and every call is noted.
#define POOLS_MAX 64
static struct { VkCommandPool pool; int allocated, destroyed; } pools[POOLS_MAX];
static int npools, pool_attempts, fail_pool, fail_alloc, alloc_calls;
static uintptr_t next_cb = 0x100000;

static int pool_index(VkCommandPool p) {
    for (int i = 0; i < npools; i++)
        if (pools[i].pool == p) return i;
    return -1;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateCommandPool(VkDevice device, const VkCommandPoolCreateInfo *info,
                                                   const VkAllocationCallbacks *alloc, VkCommandPool *pool) {
    pool_attempts++;
    CHECK(device == ctx.device, "the pool is created on another device");
    CHECK(info->sType == VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, "sType %d", (int)info->sType);
    CHECK(info->flags == VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, "flags %#x: a part's buffer is reset alone", (unsigned)info->flags);
    CHECK(info->queueFamilyIndex == ctx.gfx_family, "family %u", info->queueFamilyIndex);
    CHECK(!alloc, "allocation callbacks");
    if (fail_pool) return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    if (npools == POOLS_MAX) return VK_ERROR_OUT_OF_HOST_MEMORY;
    pools[npools].pool = (VkCommandPool)(uintptr_t)(0x1000 + npools * 0x10);
    *pool = pools[npools++].pool;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkAllocateCommandBuffers(VkDevice device, const VkCommandBufferAllocateInfo *info,
                                                        VkCommandBuffer *cbs) {
    alloc_calls++;
    int p = pool_index(info->commandPool);
    CHECK(device == ctx.device, "the buffer is allocated on another device");
    CHECK(info->sType == VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, "sType %d", (int)info->sType);
    CHECK(p >= 0 && !pools[p].destroyed, "a buffer from a pool that does not exist");
    CHECK(info->level == VK_COMMAND_BUFFER_LEVEL_PRIMARY && info->commandBufferCount == 1, "level %d count %u",
          (int)info->level, info->commandBufferCount);
    if (fail_alloc) return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    if (p >= 0) pools[p].allocated++;
    *cbs = (VkCommandBuffer)(next_cb += 0x40);
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyCommandPool(VkDevice device, VkCommandPool pool, const VkAllocationCallbacks *alloc) {
    int p = pool_index(pool);
    CHECK(device == ctx.device && !alloc, "the pool is destroyed on another device");
    CHECK(p >= 0, "a pool that was never created is destroyed");
    if (p >= 0) pools[p].destroyed++;
}

// ---- jobs
// What one job saw on its helper thread. done is its last store: the replay thread reads it right after the join.
typedef struct {
    volatile int32_t started, done, runs;
    int here, failed_before, failed_after, fail_returned, timed_out;
    uint64_t fence;
    int fail1, fail2;        // reasons the job reports, in this order (VK_SPEC_NONE: none)
    int until_failed;        // the job goes on until its part is given up (by anyone)
    uint32_t sleep_ms;
    uint64_t add, *sum;
} Job;

static void job_fn(void *arg) {
    Job *j = arg;
    __atomic_store_n(&j->started, 1, __ATOMIC_RELEASE);
    j->runs++;
    j->here = vk_spec_helper();
    j->fence = vk_spec_fence();
    j->failed_before = vk_spec_failed();
    j->fail_returned = 1;
    if (j->fail1) j->fail_returned &= vk_spec_fail(j->fail1);
    if (j->fail2) j->fail_returned &= vk_spec_fail(j->fail2);
    if (j->until_failed) {
        uint64_t t0 = plat_now_ns();
        while (!vk_spec_failed()) {
            plat_pause();
            if (plat_now_ns() - t0 > 5000000000ull) { j->timed_out = 1; break; }
        }
    }
    if (j->sleep_ms) plat_sleep_ms(j->sleep_ms);
    if (j->sum) *j->sum += j->add;
    for (volatile int i = 0; i < 200; i++) {}   // a join that does not wait returns before done is stored
    j->failed_after = vk_spec_failed();
    __atomic_store_n(&j->done, 1, __ATOMIC_RELEASE);
}

static Job J[VK_SPEC_HELPERS];
static int n;   // the helper threads that run

static void jobs_clear(void) { memset(J, 0, sizeof J); }
static void launch(int k, uint64_t fence) { vk_spec_launch(k, job_fn, &J[k], fence); }
static int join(int k) {
    int why = vk_spec_join(k);
    CHECK(__atomic_load_n(&J[k].done, __ATOMIC_ACQUIRE) == 1, "helper %d: the join returned before the job did", k);
    return why;
}
static void wait_started(int k) {
    uint64_t t0 = plat_now_ns();
    while (!__atomic_load_n(&J[k].started, __ATOMIC_ACQUIRE) && plat_now_ns() - t0 < 5000000000ull) plat_pause();
    CHECK(J[k].started, "helper %d did not start its job within 5 s", k);
}

// ---- the cases
// A job runs once, on its helper's thread, and sees the fence it was launched with.
static void identity(void) {
    CHECK(vk_spec_helper() == 0 && vk_spec_fence() == 0 && vk_spec_failed() == VK_SPEC_NONE, "the replay thread is no helper");
    jobs_clear();
    for (int k = 0; k < n; k++) launch(k, 1000 + (uint64_t)k);
    for (int k = 0; k < n; k++) {
        int why = join(k);
        CHECK(why == VK_SPEC_NONE, "helper %d: why %d", k, why);
        CHECK(J[k].runs == 1, "helper %d: %d runs", k, J[k].runs);
        CHECK(J[k].here == k + 1, "helper %d: vk_spec_here %d", k, J[k].here);
        CHECK(J[k].fence == 1000 + (uint64_t)k, "helper %d: fence %llu", k, (unsigned long long)J[k].fence);
        CHECK(J[k].failed_before == VK_SPEC_NONE && J[k].failed_after == VK_SPEC_NONE, "helper %d: a part that stood reads as failed", k);
        CHECK(vk_spec_part_failed(k) == VK_SPEC_NONE, "helper %d: vk_spec_part_failed %d", k, vk_spec_part_failed(k));
    }
}

// A helper's first reason is its part's, the other helpers' parts stand, and the next part starts clean.
static void failing(void) {
    for (int bad = 0; bad < n; bad++) {
        jobs_clear();
        J[bad].fail1 = VK_SPEC_UPLOAD, J[bad].fail2 = VK_SPEC_WRITE;
        for (int k = 0; k < n; k++) launch(k, 7);
        for (int k = 0; k < n; k++) {
            int why = join(k), want = k == bad ? VK_SPEC_UPLOAD : VK_SPEC_NONE;
            CHECK(why == want, "helper %d of %d failing: why %d", k, bad, why);
            CHECK(J[k].failed_after == want, "helper %d: vk_spec_failed %d on its thread", k, J[k].failed_after);
            CHECK(J[k].fail_returned == 1, "vk_spec_fail returns 1");
            CHECK(vk_spec_part_failed(k) == want, "helper %d: vk_spec_part_failed %d", k, vk_spec_part_failed(k));
        }
        jobs_clear();
        launch(bad, 8);
        CHECK(join(bad) == VK_SPEC_NONE && J[bad].failed_before == VK_SPEC_NONE, "helper %d: the last part's reason leaked", bad);
    }
}

// The replay thread gives every part that is out up, and a part a helper already gave up keeps its own reason.
static void cancelling(void) {
    jobs_clear();
    for (int k = 0; k < n; k++) J[k].until_failed = 1;
    if (n > 1) J[n - 1].fail1 = VK_SPEC_TEXTURE;
    for (int k = 0; k < n; k++) launch(k, 9);
    for (int k = 0; k < n; k++) wait_started(k);
    if (n > 1) {   // the last helper's own reason is in before the cancel
        uint64_t t0 = plat_now_ns();
        while (vk_spec_part_failed(n - 1) != VK_SPEC_TEXTURE && plat_now_ns() - t0 < 5000000000ull) plat_pause();
    }
    for (int k = 0; k < n; k++)
        if (!(n > 1 && k == n - 1)) CHECK(!__atomic_load_n(&J[k].done, __ATOMIC_ACQUIRE), "helper %d left a part that stood", k);
    CHECK(vk_spec_fail(VK_SPEC_CANCEL) == 1, "vk_spec_fail returns 1");
    for (int k = 0; k < n; k++) {
        int why = join(k), want = n > 1 && k == n - 1 ? VK_SPEC_TEXTURE : VK_SPEC_CANCEL;
        CHECK(why == want, "helper %d: why %d, wanted %d", k, why, want);
        CHECK(!J[k].timed_out, "helper %d never saw its part given up", k);
        CHECK(J[k].failed_after == want, "helper %d: vk_spec_failed %d", k, J[k].failed_after);
    }
    // Given up while idle (the flags above are still set, and one more cancel): the next part starts clean.
    vk_spec_fail(VK_SPEC_CANCEL);
    jobs_clear();
    for (int k = 0; k < n; k++) launch(k, 10);
    for (int k = 0; k < n; k++) CHECK(join(k) == VK_SPEC_NONE && J[k].failed_before == VK_SPEC_NONE, "helper %d: a cancel while idle leaked", k);
}

// Many handoffs in a row on every helper at once: each job runs once, with its own argument and fence, and each join
// returns that job's reason.
static void rounds(void) {
    enum { ROUNDS = 20000 };
    static uint64_t sum[VK_SPEC_HELPERS];
    uint64_t want[VK_SPEC_HELPERS] = {0};
    int bad_why = 0, bad_fence = 0, bad_here = 0, bad_runs = 0;
    for (uint32_t r = 0; r < ROUNDS; r++) {
        jobs_clear();
        for (int k = 0; k < n; k++) {
            J[k].add = (uint64_t)r * 31 + (uint64_t)k + 1, J[k].sum = &sum[k];
            want[k] += J[k].add;
            if ((r + (uint32_t)k) % 3 == 0) J[k].fail1 = 1 + (int)((r + (uint32_t)k) % (VK_SPEC_N - 1));
            launch(k, ((uint64_t)r << 8) | (uint64_t)k);
        }
        for (int k = 0; k < n; k++) {
            int why = join(k);
            bad_why += why != J[k].fail1;
            bad_fence += J[k].fence != (((uint64_t)r << 8) | (uint64_t)k);
            bad_here += J[k].here != k + 1;
            bad_runs += J[k].runs != 1;
        }
    }
    CHECK(!bad_why && !bad_fence && !bad_here && !bad_runs, "%d wrong reasons, %d wrong fences, %d wrong threads, %d wrong run counts",
          bad_why, bad_fence, bad_here, bad_runs);
    for (int k = 0; k < n; k++)
        CHECK(sum[k] == want[k], "helper %d: sum %llu, wanted %llu", k, (unsigned long long)sum[k], (unsigned long long)want[k]);
}

// A helper that went to sleep takes the next job, and a join outlasts a job longer than its spin.
static void sleeping(void) {
    for (int round = 0; round < 3; round++) {
        plat_sleep_ms(20);   // well past the helpers' spin: they sleep
        jobs_clear();
        uint64_t t0 = plat_now_ns();
        for (int k = 0; k < n; k++) launch(k, 11);
        for (int k = 0; k < n; k++) CHECK(join(k) == VK_SPEC_NONE && J[k].runs == 1, "helper %d: a sleeping helper's job", k);
        CHECK(plat_now_ns() - t0 < 2000000000ull, "sleeping helpers took %llu ms", (unsigned long long)((plat_now_ns() - t0) / 1000000));
    }
    jobs_clear();
    for (int k = 0; k < n; k++) J[k].sleep_ms = 30, J[k].fail1 = k & 1 ? VK_SPEC_MIRROR : VK_SPEC_NONE;
    uint64_t t0 = plat_now_ns();
    for (int k = 0; k < n; k++) launch(k, 12);
    for (int k = 0; k < n; k++) CHECK(join(k) == J[k].fail1, "helper %d: the reason of a long job", k);
    CHECK(plat_now_ns() - t0 >= 25000000ull, "the joins returned after %llu us of a 30 ms job", (unsigned long long)((plat_now_ns() - t0) / 1000));
}

// The command buffers of a helper's parts: a pool of its own, a buffer held until its submission's value is done or
// its part is dropped, 32 of them at most.
static void command_buffers(void) {
    int last = n - 1;
    fence_done = 0;
    fail_pool = 1;
    CHECK(vk_spec_cb(0, 5) == VK_NULL_HANDLE && pool_attempts == 1 && npools == 0, "a pool that cannot be created gives no buffer");
    fail_pool = 0;
    fail_alloc = 1;
    CHECK(vk_spec_cb(0, 5) == VK_NULL_HANDLE && pool_attempts == 2 && npools == 1, "a failed creation is tried again; a failed allocation gives no buffer");
    fail_alloc = 0;
    VkCommandBuffer a = vk_spec_cb(0, 10), b = vk_spec_cb(0, 11);
    CHECK(a && b && a != b, "two parts whose submissions are not done share a buffer");
    CHECK(npools == 1 && pools[0].allocated == 2, "%d pools, %d buffers", npools, pools[0].allocated);
    CHECK(vk_spec_cb(0, 12) != a, "a buffer is reused before its submission is done");   // a third
    fence_done = 10;
    CHECK(vk_spec_cb(0, 13) == a, "the buffer of a done submission is not reused");
    CHECK(vk_spec_cb(0, 14) != a && pools[0].allocated == 4, "a buffer in use is handed out again");
    fence_done = 9;   // a is at 13 again
    VkCommandBuffer d = vk_spec_cb(0, 15);
    CHECK(d != a && d != b, "a new part took a buffer that is out");
    vk_spec_cb_drop(0);
    CHECK(vk_spec_cb(0, 16) == d, "a dropped part's buffer is free at once");
    vk_spec_cb_drop(0);
    vk_spec_cb_drop(0);   // nothing current: nothing more is freed
    CHECK(vk_spec_cb(0, 17) == d, "the dropped buffer");
    int before = pools[0].allocated;
    CHECK(vk_spec_cb(0, 18) != d && pools[0].allocated == before + 1, "a second drop freed a buffer that is out");
    CHECK(npools == 1, "helper 0 made %d pools", npools);

    // 32 buffers out, on another helper when there is one: its pool is its own.
    fence_done = 0;
    int k = last, p = k ? npools : 0, have = k ? 0 : pools[0].allocated;
    VkCommandBuffer first = VK_NULL_HANDLE, seen[32];
    for (int i = have; i < 32; i++) {
        VkCommandBuffer cb = vk_spec_cb(k, 100 + (uint64_t)i);
        CHECK(cb != VK_NULL_HANDLE, "buffer %d of 32", i);
        for (int e = have; e < i; e++) CHECK(seen[e] != cb, "buffers %d and %d are one", e, i);
        seen[i] = cb;
        if (i == have) first = cb;
    }
    CHECK(npools == (k ? 2 : 1), "%d pools for %d helpers with buffers", npools, k ? 2 : 1);
    CHECK(pools[p].allocated == 32, "%d buffers in the pool", pools[p].allocated);
    int calls = alloc_calls;
    CHECK(vk_spec_cb(k, 200) == VK_NULL_HANDLE && alloc_calls == calls, "a 33rd buffer");
    if (k) {
        fence_done = 100;
        CHECK(vk_spec_cb(k, 201) == first, "after the limit, the buffer of the submission that is done");
    }

    // The device goes: every pool is destroyed once, and the next part gets a new pool.
    int made = npools;
    vk_spec_shutdown();
    for (int i = 0; i < made; i++) CHECK(pools[i].destroyed == 1, "pool %d destroyed %d times", i, pools[i].destroyed);
    vk_spec_shutdown();
    for (int i = 0; i < made; i++) CHECK(pools[i].destroyed == 1, "pool %d destroyed %d times by a second shutdown", i, pools[i].destroyed);
    fence_done = 0;
    VkCommandBuffer fresh = vk_spec_cb(0, 300);
    CHECK(fresh && npools == made + 1 && pools[made].allocated == 1, "no new pool after the shutdown");
    ctx_gone = 1;   // no context any more: nothing to destroy with
    vk_spec_shutdown();
    ctx_gone = 0;
    CHECK(pools[made].destroyed == 0, "a pool destroyed without a context");
    CHECK(vk_spec_cb(0, 301) && npools == made + 2, "the pool of a context that is gone is forgotten");
}

// vk_spec.h: five helpers from 24 logical processors, four from 16, three from 8, two from 4, one below. Each row is
// a count on either side of a step, so moving a step by one processor fails a row.
static int counts(void) {
    static const struct { uint32_t cpus; int helpers; } rows[] = {
        {0, 1}, {1, 1}, {2, 1}, {3, 1}, {4, 2}, {6, 2}, {7, 2}, {8, 3}, {12, 3}, {15, 3},
        {16, 4}, {20, 4}, {23, 4}, {24, 5}, {32, 5}, {64, 5}, {UINT32_MAX, 5},
    };
    for (size_t i = 0; i < sizeof rows / sizeof rows[0]; i++)
        CHECK(vk_spec_helpers_for(rows[i].cpus) == rows[i].helpers, "%u logical processors: %d helpers, expected %d",
              rows[i].cpus, vk_spec_helpers_for(rows[i].cpus), rows[i].helpers);
    for (uint32_t cpus = 1; cpus < 128; cpus++) {   // more processors never give fewer helpers, nor more than fit
        int k = vk_spec_helpers_for(cpus);
        CHECK(k >= vk_spec_helpers_for(cpus - 1) && k >= 1 && k <= VK_SPEC_HELPERS, "%u logical processors: %d helpers",
              cpus, k);
    }
    printf("vk_spec helper counts: %d checks, %d failed\n", checks, failures);
    return failures != 0;
}

int main(int argc, char **argv) {
    if (argc == 2 && !strcmp(argv[1], "counts")) return counts();
    if (argc != 4) return fprintf(stderr, "usage: %s <TOTK_REPLAY_PAR or -> <TOTK_REPLAY_HELPERS or -> <helpers or cpu>\n"
                                          "       %s counts\n", argv[0], argv[0]), 2;
    setvbuf(stdout, NULL, _IONBF, 0);
    par_value = strcmp(argv[1], "-") ? argv[1] : NULL;
    helpers_value = strcmp(argv[2], "-") ? argv[2] : NULL;
    int expected = !strcmp(argv[3], "cpu") ? vk_spec_helpers_for(plat_cpu_usable()) : atoi(argv[3]);
    int ab = par_value && !strcmp(par_value, "ab"), check = par_value && !strcmp(par_value, "check");
    ctx.device = (VkDevice)(uintptr_t)0xD0D0, ctx.gfx_family = 3;

    CHECK(vk_spec_count() == 0, "helper threads before the first vk_spec_on");
    ab_block = 1;
    int on = vk_spec_on();
    n = vk_spec_count();
    CHECK(on == (expected > 0), "vk_spec_on %d with TOTK_REPLAY_PAR=%s", on, argv[1]);
    CHECK(n == expected, "%d helper threads, expected %d", n, expected);
    CHECK(vk_spec_checking() == check, "vk_spec_checking %d", vk_spec_checking());
    CHECK(strstr(logged, expected ? "helper threads" : "translated by the replay thread alone") != NULL, "the log line: %s", logged);
    char count[48];
    snprintf(count, sizeof count, "%d helper thread%s)", expected, expected == 1 ? "" : "s");
    CHECK(strstr(logged, count) != NULL, "the log line: %s", logged);
    size_t once = strlen(logged);
    ab_block = 0;
    CHECK(vk_spec_on() == (expected > 0 && !ab), "TOTK_REPLAY_PAR=%s in an A block: vk_spec_on %d", argv[1], vk_spec_on());
    ab_block = 1;
    CHECK(vk_spec_on() == (expected > 0) && vk_spec_count() == expected && strlen(logged) == once, "a second vk_spec_on changed something");

    if (n) {
        identity();
        failing();
        cancelling();
        rounds();
        sleeping();
        command_buffers();
        identity();   // the helpers outlive vk_spec_shutdown
    }
    printf("vk_spec handoff (TOTK_REPLAY_PAR=%s, TOTK_REPLAY_HELPERS=%s): %d helper threads, %d checks, %d failed\n",
           argv[1], argv[2], n, checks, failures);
    return failures != 0;
}
