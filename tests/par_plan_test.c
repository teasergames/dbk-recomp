// par_plan and par_count of runtime/vk_exec.c, without a device: par_plan_test.py inserts the production constants,
// the ParSlot type and the functions unchanged. par_plan picks, from what a run's gen and ev bits say, the draws the
// replay thread leaves to the helper threads: launched after l draws, parts from draw c to draw y, k parts.
// The cases are the counts docs/architecture.md gives, plans worked out by hand, and properties every plan has
// whatever the bits are.
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* VK_SPEC_HELPERS is inserted here by par_plan_test.py */

static int helpers;   // the helper threads that run
static int vk_spec_count(void) { return helpers; }

/* production code is inserted here by par_plan_test.py */

static int failures, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { failures++; printf("FAIL line %d: %s: ", __LINE__, #c); printf(__VA_ARGS__); printf("\n"); } } while (0)

static ParSlot S;

static void clear(void) { memset(&S, 0, sizeof S); }

// The plan of a run of n draws with `count` helper threads running.
typedef struct { uint32_t k, l, c, y; } Plan;
static Plan plan(uint32_t n, int count) {
    Plan p = {0, 0, 0, 0};
    helpers = count;
    p.k = par_plan(&S, n, &p.l, &p.c, &p.y);
    return p;
}

#define EXPECT(p, K, L, C, Y) CHECK((p).k == (K) && (!(K) || ((p).l == (L) && (p).c == (C) && (p).y == (Y))), \
                                    "got k %u l %u c %u y %u", (p).k, (p).l, (p).c, (p).y)

// The helpers a stretch of `span` draws gets (docs/architecture.md: two from 640 draws, three from 1,440, four from
// 2,560; five from 4,000), limited by the threads that run.
static uint32_t count_for(uint32_t span, int running) {
    uint32_t k = span >= 4000 ? 5 : span >= 2560 ? 4 : span >= 1440 ? 3 : span >= 640 ? 2 : 1;
    return k > (uint32_t)running ? (uint32_t)running : k;
}

static void counts(void) {
    static const struct { uint32_t span, k; } at[] = {{1, 1}, {639, 1}, {640, 2}, {1439, 2}, {1440, 3}, {2559, 3},
                                                      {2560, 4}, {3999, 4}, {4000, 5}, {8192, 5}};
    for (size_t i = 0; i < sizeof at / sizeof at[0]; i++) {
        helpers = VK_SPEC_HELPERS;
        CHECK(par_count(at[i].span) == at[i].k, "span %u gives %u", at[i].span, par_count(at[i].span));
        for (int running = 1; running <= VK_SPEC_HELPERS; running++) {
            helpers = running;
            uint32_t want = at[i].k < (uint32_t)running ? at[i].k : (uint32_t)running;
            CHECK(par_count(at[i].span) == want, "span %u with %d threads gives %u", at[i].span, running, par_count(at[i].span));
        }
    }
}

static void examples(void) {
    // A run nothing is known about: launched at once, the replay thread keeps PAR_PACE / (k + PAR_PACE) of the draws.
    clear();
    Plan p = plan(1000, 5);
    EXPECT(p, 2, 0, 500, 1000);
    p = plan(1000, 1);
    EXPECT(p, 1, 0, 667, 1000);
    p = plan(4001, 5);          // five helpers: the replay thread keeps two sevenths, 1143.14 draws
    EXPECT(p, 5, 0, 1144, 4001);
    p = plan(4001, 3);          // three threads run: two fifths, 1600.4 draws
    EXPECT(p, 3, 0, 1601, 4001);

    // The learned share is used for the helper count it was learned with, and only for that one.
    clear();
    S.ks = 2, S.share = 0.25f;
    p = plan(1000, 5);
    EXPECT(p, 2, 0, 250, 1000);
    p = plan(1000, 1);
    EXPECT(p, 1, 0, 667, 1000);
    S.share = 0.75f;
    p = plan(1000, 5);
    EXPECT(p, 2, 0, 750, 1000);

    // A part pays for its launch and join from PAR_PART_MIN (48) draws.
    clear();
    p = plan(142, 1);           // the cut falls at 95: 47 draws
    EXPECT(p, 0, 0, 0, 0);
    p = plan(145, 1);           // the cut falls at 97: 48 draws
    EXPECT(p, 1, 0, 97, 145);
    p = plan(0, 5);
    EXPECT(p, 0, 0, 0, 0);

    // A draw the helper cannot hold (ev) at 611. Before it the parts end PAR_GUARD draws early, at 602, and the cut is
    // at 402: 200 draws. After it they may begin at 620 and run to the end: 380 draws, with the launch still at 0, so
    // the stretch from the launch is 1000 draws and takes two helpers.
    clear();
    par_bit(S.ev, 611, 1);
    p = plan(1000, 5);
    EXPECT(p, 2, 0, 620, 1000);
    // The same draw changing what a part reads (gen): the helpers of the later stretch cannot be launched before draw
    // 620, which leaves the replay thread 254 draws of its own and the helper 126. The earlier stretch is the longer.
    clear();
    par_bit(S.gen, 611, 1);
    p = plan(1000, 5);
    EXPECT(p, 1, 0, 402, 602);
    // Further in, the later stretch wins: launched at 220, 780 draws to the end, two helpers, the cut in the middle.
    clear();
    par_bit(S.gen, 211, 1);
    p = plan(1000, 5);
    EXPECT(p, 2, 220, 610, 1000);

    // Bits after the last draw (bit n) end the parts early like any other.
    clear();
    par_bit(S.ev, 1000, 1);
    p = plan(1000, 1);          // parts end at 991, the cut at 661
    EXPECT(p, 1, 0, 661, 991);

    // A run with a draw the helper cannot hold every 40 draws has no room for a part.
    clear();
    for (uint32_t i = 20; i <= 1000; i += 40) par_bit(S.ev, i, 1);
    p = plan(1000, 5);
    EXPECT(p, 0, 0, 0, 0);
}

static uint64_t rng = 0x9E3779B97F4A7C15ull;
static uint32_t rnd(uint32_t n) {   // 0 to n - 1
    rng ^= rng << 13, rng ^= rng >> 7, rng ^= rng << 17;
    return (uint32_t)((rng >> 20) % n);
}

// Whatever the bits: a plan lies inside the run, its parts are long enough, nothing the helpers cannot hold and no
// change of what they read lies within PAR_GUARD draws of the parts, no such change lies between the launch and the
// end of the parts, the launch is the earliest one that allows, and the helper count is the one for the stretch.
static void properties(void) {
    int planned = 0;
    for (int round = 0; round < 20000; round++) {
        clear();
        uint32_t n = round % 7 ? rnd(PAR_RUN_MAX + 1) : rnd(600);
        int marks = (int)rnd(12);
        for (int m = 0; m < marks; m++) par_bit(rnd(2) ? S.gen : S.ev, rnd(n + 1), 1);
        if (rnd(2)) S.ks = 1 + rnd(VK_SPEC_HELPERS), S.share = 0.05f + (float)rnd(90) / 100.0f;
        int running = 1 + (int)rnd(VK_SPEC_HELPERS);
        Plan p = plan(n, running);
        if (!p.k) continue;
        planned++;
        CHECK(p.k <= (uint32_t)running, "round %d: k %u with %d threads", round, p.k, running);
        CHECK(p.l < p.c && p.c < p.y && p.y <= n, "round %d: l %u c %u y %u n %u", round, p.l, p.c, p.y, n);
        CHECK(p.y - p.c >= PAR_PART_MIN, "round %d: parts of %u draws", round, p.y - p.c);
        CHECK(p.k == count_for(p.y - p.l, running), "round %d: k %u for %u draws", round, p.k, p.y - p.l);
        uint32_t end = p.y + PAR_GUARD < n ? p.y + PAR_GUARD : n;
        for (uint32_t i = p.c > PAR_GUARD ? p.c - PAR_GUARD : 0; i <= end; i++)
            CHECK(!par_bit_at(S.gen, i) && !par_bit_at(S.ev, i), "round %d: bit at %u, parts %u to %u", round, i, p.c, p.y);
        for (uint32_t i = p.l > PAR_GUARD ? p.l - PAR_GUARD : 0; i <= end; i++)
            CHECK(!par_bit_at(S.gen, i), "round %d: gen at %u, launch %u, end %u", round, i, p.l, p.y);
        if (p.l)
            CHECK(p.l > PAR_GUARD && par_bit_at(S.gen, p.l - PAR_GUARD - 1), "round %d: launch %u is later than a gen bit asks", round, p.l);
    }
    CHECK(planned > 5000, "only %d of 20000 random runs got a plan", planned);
}

int main(void) {
    counts();
    examples();
    properties();
    printf("par_plan: %d checks, %d failed\n", checks, failures);
    return failures != 0;
}
