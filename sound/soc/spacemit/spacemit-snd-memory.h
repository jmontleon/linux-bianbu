// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2022 SPACEMIT Micro Limited
 */

#ifndef _SPACEMIT_SND_MEMORY_H
#define _SPACEMIT_SND_MEMORY_H

#include <linux/kernel.h>
#include <linux/list.h>

enum {
	MEMORY_DIR_OUT,
	MEMORY_DIR_IN
};

struct audio_buffer {
	union {
		size_t offset;
		size_t filled;
	};
	size_t size;
	void *vaddr;
	dma_addr_t paddr;
};

struct audio_memory_manager;
int spacemit_snd_alloc_memory(struct audio_memory_manager **memmgr, int dir, int buf_num, size_t buf_size);
struct audio_buffer *spacemit_snd_get_next_buffer(struct audio_memory_manager *memmgr, int wait);
struct audio_buffer *spacemit_snd_get_free_buffer(struct audio_memory_manager * memmgr);
struct audio_buffer *spacemit_snd_get_first_rendered_buffer(struct audio_memory_manager *memmgr);
int spacemit_snd_finish_one_buffer(struct audio_memory_manager *memmgr, struct audio_buffer *abuf);
int spacemit_snd_free_memory(struct audio_memory_manager *memmgr);
void *spacemit_snd_get_memory_vaddr(struct audio_memory_manager *memmgr);
int spacemit_snd_get_memory_paddr(struct audio_memory_manager *memmgr, dma_addr_t *paddr);

#endif

