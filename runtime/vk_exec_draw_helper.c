// The draw translator of the helper threads (vk_spec.h): vk_exec_draw.c compiled once more, with VK_EXEC_DRAW_HELPER.
// Every helper runs this one copy, each on its own draw context (VkExec.draw), while the replay thread runs the first
// copy on draw_main, which it addresses directly. vk_exec_draw.c (the head of the file) lists what the copies share
// and how the replay thread's entry points pass a helper's VkExec on to this copy's (vk_exec_draw_h_*).
#define VK_EXEC_DRAW_HELPER 1
// What only the replay thread runs is still compiled here (the stand-in setup, the depth twins of dispatches): unused.
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-variable"
#include "vk_exec_draw.c"
