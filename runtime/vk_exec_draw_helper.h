// The entry points of the helper threads' copy of the draw translator (vk_exec_draw_helper.c: vk_exec_draw.c compiled
// again with VK_EXEC_DRAW_HELPER). They are vk_exec_draw.c's own under these names; the replay thread's copy passes a
// VkExec whose draw context is a helper's on to them (ON_HELPER), and nothing else calls them.
#pragma once
#include <stdint.h>

struct VkExec;
void vk_exec_draw_h_op_draw(struct VkExec *ex, uint32_t op, const void *payload);
void vk_exec_draw_h_new_cb(struct VkExec *ex);
void vk_exec_draw_h_rebase(struct VkExec *ex);
void vk_exec_draw_h_digest_begin(const struct VkExec *ex);
void vk_exec_draw_h_digest_draw(const struct VkExec *ex);
uint32_t vk_exec_draw_h_digest_end(const struct VkExec *ex, const uint64_t **d);
int vk_exec_draw_h_reset_indirect(struct VkExec *ex, uint64_t list_addr, uint32_t descriptor_bytes, int64_t first,
                                  int64_t count);
