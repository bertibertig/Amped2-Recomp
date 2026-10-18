/*
 * Vulkan render back end for the NV2A pushbuffer executor.
 *
 * Replaces the executor's CPU rasteriser: the runtime still decodes the
 * pushbuffer and transforms vertices, this draws the result on the GPU and
 * presents it in its own window. Design and milestones: docs/vulkan-backend.md.
 */
#ifndef AMPED2_VK_BACKEND_H
#define AMPED2_VK_BACKEND_H

/* Register the back end with the executor (nv2a_backend_register). Call once
 * before the guest starts; Vulkan and the window come up lazily on the NV2A
 * poll thread at the first clear/draw/flip. Returns 0 on success. */
int amped2_vk_register(void);

/* POSIX entry point when the Vulkan renderer is selected, instead of
 * xbox_HostWindowMain: opens the window on the calling (main) thread, runs
 * guest_main on its own thread and pumps window events from the start, so the
 * window stays responsive through the load. Returns guest_main's result. */
int amped2_vk_host_main(int (*guest_main)(void));

#endif /* AMPED2_VK_BACKEND_H */
