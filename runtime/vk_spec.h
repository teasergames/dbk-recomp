// The helper translation threads of the Vulkan replay (TOTK_REPLAY_PAR, default 0: the replay thread translates every
// draw; 1 turns them on, check has them and the replay thread translate every part and compares, ab alternates with
// the A/B blocks). A submission is replayed by one thread, and most of that time goes into long runs of draws that
// only bind, look up and record. When the replay thread enters such a run it hands the draws from a cut to an end
// (vk_exec.c par_plan) to the helpers, one consecutive part each, and every helper translates its part into a command
// buffer of its own while the replay thread does the draws before the cut; from the cut on the replay thread takes the
// helpers' command buffers in order as the next parts of the submission (vk_exec.c par_join, vk_sync.c
// vk_sync_split_insert) and goes on after the last of them.
//
// There are up to VK_SPEC_HELPERS of them. TOTK_REPLAY_HELPERS says how many; without it the logical processors of
// the process do (vk_spec_helpers_for). On a Ryzen 7 5700X3D (16 logical processors) a scene of 27,000 draws took
// 45 ms a frame with one, 41 with two, 36 with three, 34 with four and 33 with five. Held to its first 8 logical
// processors it took 71 ms with none, 52 with one, 44 with two and 42 with three; held to 4, 74 with none, 58 with
// one and 52 with two. How many of them a run uses depends on its length (vk_exec.c par_count).
//
// A helper works on a guess: the state the replay thread will have at the meeting point, derived from the ops alone.
// Whatever it cannot do without changing what the replay thread sees (a GPU write, a texture upload, a new image, a
// pipeline that is still building) it does not do: it gives its part up (vk_spec_fail) and the replay thread translates
// those ops itself, as if no helper had run. A part is also dropped when the replay thread changed something the helper
// may have read meanwhile (vk_spec_world). So a part is either what the replay thread would have recorded or it is
// thrown away: a helper never changes what is drawn.
//
// A helper does create and write a few things. Each is what the replay thread would have made at the same draw, each
// is made under the lock its owner already takes, and none has to be undone when the part is dropped:
// - image views of registered textures, in the one cache every thread's draws use (vk_exec_draw.c get_view and
//   vk_exec_target_view; texture lock), kept for the image's lifetime;
// - views of targets that are not registered textures, in the list of the helper's own VkExec (vk_exec.c view_of),
//   released at the submission's fence whether the part is taken or not (vk_exec.c par_join and par_close);
// - a shader or pipeline that is not built yet: its lookup enters it and queues the build as the replay thread's
//   would (gfx_shader_get_async; vk_pipeline.c graphics, under the cache lock), and then the helper gives its part up
//   (VK_SPEC_PENDING);
// - the freshness marks of a texture it found unchanged in this submission (vk_texture.c Tex.wgen and Tex.checked,
//   TexSlot.cepoch and the slot's resolved entry; texture lock);
// - this submission's sync of a pool's mirror (vk_memory.c mirror_sync, under mirror_lock), a little earlier than
//   the replay thread's first use.
#pragma once
#include <stdint.h>

#include "vk.h"

// Why a part was given up (the perf line counts them by reason).
enum {
    VK_SPEC_NONE,
    VK_SPEC_WRITE,      // a draw writes GPU memory: the order of writes and read-backs is the replay thread's
    VK_SPEC_STANDIN,    // a stand-in image or view that does not exist yet
    VK_SPEC_FEEDBACK,   // a target learned to be sampled by its own pass: the open pass changes
    VK_SPEC_PENDING,    // a shader or pipeline still building: whether to wait or skip is decided on the replay thread
    VK_SPEC_TEXTURE,    // a texture entry that has to be created, taken back into use or re-described
    VK_SPEC_UPLOAD,     // a texture whose storage changed: the upload belongs to the replay thread
    VK_SPEC_MIRROR,     // a pool without a VRAM mirror yet
    VK_SPEC_TARGET,     // a render target the helper may not resolve (a new stand-in, a first GPU write)
    VK_SPEC_OP,         // an op variant the helper does not replay (a masked stencil clear)
    VK_SPEC_WORLD,      // the replay thread changed textures or mirrors while the helper read them
    VK_SPEC_USNAP,      // a uniform snapshot was dropped meanwhile
    VK_SPEC_STATE,      // the replay state at the meeting point is not the one the helper started from
    VK_SPEC_CANCEL,     // the replay thread left the run early
    VK_SPEC_N
};

#define VK_SPEC_HELPERS 5                              // helper threads at most: vk_exec_draw.c has a draw context for each

extern _Thread_local int vk_spec_here;                 // on a helper thread 1 + its number, 0 on every other thread
static inline int vk_spec_helper(void) { return vk_spec_here; }

// Helper thread: gives its part up for the first reason reported. Always 1, so a caller can write
// `if (cannot && vk_spec_fail(why)) return ...;`. Replay thread: every part that is out is given up for why
// (VK_SPEC_CANCEL, or what the replay thread changed), and each helper stops at its next op.
int vk_spec_fail(int why);
int vk_spec_failed(void);                              // helper thread: the reason so far, VK_SPEC_NONE while its part stands
int vk_spec_part_failed(int k);                        // replay thread: the same of helper k's part
uint64_t vk_spec_fence(void);                          // helper thread: the value of the submission it translates for

// Bumped by the replay thread's side when it changes something a helper part may have read and that the ops do not
// show: a texture entry created or taken back into use, a texture's first GPU write, a learned feedback target.
void vk_spec_world_changed(void);
uint64_t vk_spec_world(void);

// Counted where a thread other than the helper does what the helper gives a part up for (a GPU write into guest memory,
// a texture upload, a new stand-in): from the count the replay thread learns which draws of a run are its own to
// translate (vk_exec.c par_watch).
void vk_spec_event(void);
uint32_t vk_spec_events(void);

// ---- replay thread. k is a helper's number, 0 to vk_spec_count() - 1.
int vk_spec_on(void);                                  // the switch is on now and the threads run
int vk_spec_count(void);                               // the helper threads that run (0 before the first vk_spec_on)
// The helper threads started without TOTK_REPLAY_HELPERS, for a process with this many logical processors: five from
// 24, four from 16, three from 8, two from 4, one below (each helper wants a processor to itself next to the replay
// thread, the recorder and the game's own threads).
int vk_spec_helpers_for(uint32_t cpus);
// TOTK_REPLAY_PAR=check: every part is translated by its helper and by the replay thread and compared; the replay
// thread's is the one submitted (vk_exec.c par_check).
int vk_spec_checking(void);
// A command buffer for helper k's next part of the submission of value fence, or VK_NULL_HANDLE when none is free.
// Only while that helper is idle: the pool is the helper's while it records.
VkCommandBuffer vk_spec_cb(int k, uint64_t fence);
void vk_spec_cb_drop(int k);                           // the part was dropped: its command buffer is free again
typedef void (*VkSpecFn)(void *arg);
void vk_spec_launch(int k, VkSpecFn fn, void *arg, uint64_t fence);
int vk_spec_join(int k);                               // waits for fn to return; the reason it gave up, or VK_SPEC_NONE
// TOTK_TRACE=perf: one more part (why VK_SPEC_NONE: taken) of draws draws, after the replay thread waited wait_ns for
// it and the helper spent busy_ns on it.
void vk_spec_note(int why, uint32_t draws, uint64_t wait_ns, uint64_t busy_ns);
void vk_spec_log(void);                                // the perf line, with the counters reset
void vk_spec_shutdown(void);                           // the device is idle: the helpers' command pools go
