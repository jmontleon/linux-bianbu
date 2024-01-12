// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2022 SPACEMIT Micro Limited
 */

#include <linux/types.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/wait.h>
#include <linux/atomic.h>
#include <linux/mm.h>
#include <linux/highmem.h>
#include <linux/mm_types.h>
#include <linux/vmalloc.h>

#include <soc/spacemit/adsp_ipc.h>

#include "spacemit-snd-memory.h"

struct _audio_buffer {
	struct audio_buffer buffer;
	struct list_head node;
};

struct audio_memory_manager {
	int dir;
	size_t size;

	void *vaddr;
	dma_addr_t paddr;

	struct _audio_buffer *_abuf;
	struct list_head rendered_list; // buffers that have been rendered to ADSP
	struct list_head ready_list; // buffer that can be accessed by AP
	struct list_head free_list; // buffers that cannot be accessed by AP, nor are they renderd to ADSP

	spinlock_t spin;
	wait_queue_head_t w;
};

int spacemit_snd_alloc_memory(struct audio_memory_manager **memmgr, int dir, int buf_cnt, size_t buf_size)
{
	int i;
	size_t total;
	struct _audio_buffer *_abuf;
	struct audio_memory_manager *_memmgr;

	pr_debug("enter %s, buffer count: %d, buffer size: %zd\n", __func__,
		buf_cnt, buf_size);

	if (!memmgr || buf_cnt <= 0 || 0 == buf_size)
		return -EINVAL;

	*memmgr = NULL;
	_memmgr = kzalloc(sizeof(struct audio_memory_manager), GFP_KERNEL);
	if (!_memmgr)
		return -ENOMEM;

	_memmgr->dir = dir;

	INIT_LIST_HEAD(&_memmgr->rendered_list);
	INIT_LIST_HEAD(&_memmgr->ready_list);
	INIT_LIST_HEAD(&_memmgr->free_list);

	spin_lock_init(&_memmgr->spin);
	init_waitqueue_head(&_memmgr->w);

	total = buf_cnt*buf_size;
	_memmgr->size = total;

	_memmgr->vaddr = adsp_ipc_shm_alloc(total, &_memmgr->paddr);
	if (!_memmgr->vaddr)
		goto fail0;

	_abuf = kzalloc(sizeof(struct _audio_buffer)*buf_cnt, GFP_KERNEL);
	if (!_abuf)
		goto fail1;

	_memmgr->_abuf = _abuf;

	for (i = 0; i < buf_cnt; i++) {
		_abuf[i].buffer.size = buf_size;
		_abuf[i].buffer.vaddr = _memmgr->vaddr + buf_size*i;
		_abuf[i].buffer.paddr = _memmgr->paddr + buf_size*i;

		if (MEMORY_DIR_OUT == _memmgr->dir) {
			list_add_tail(&_abuf[i].node, &_memmgr->ready_list);
		} else
			list_add_tail(&_abuf[i].node, &_memmgr->free_list);
	}

	*memmgr = _memmgr;

	return 0;

fail1:
	adsp_ipc_shm_free(_memmgr->vaddr, _memmgr->size);
fail0:
	kfree(_memmgr);

	return -ENOMEM;
}

struct audio_buffer *spacemit_snd_get_next_buffer(struct audio_memory_manager * memmgr, int msecs)
{
	int timeout = 0;
	DEFINE_WAIT(wait);
	struct _audio_buffer *_abuf = NULL;
	if (!memmgr)
		return NULL;

	if (msecs > 0)
		timeout = msecs_to_jiffies(msecs);

	do {
		spin_lock(&memmgr->spin);
		if (list_empty(&memmgr->ready_list)) {
			if (timeout) {
				spin_unlock(&memmgr->spin);

				prepare_to_wait(&memmgr->w, &wait, TASK_INTERRUPTIBLE);
				timeout = schedule_timeout(timeout);
				finish_wait(&memmgr->w, &wait);
				continue;
			}
		} else {
			_abuf = list_first_entry(&memmgr->ready_list, struct _audio_buffer, node);
			list_move_tail(&_abuf->node, &memmgr->rendered_list);
		}
		spin_unlock(&memmgr->spin);
	} while(!_abuf && timeout);

	return _abuf ? &_abuf->buffer : NULL;
}

struct audio_buffer *spacemit_snd_get_free_buffer(struct audio_memory_manager * memmgr)
{
	struct _audio_buffer *_abuf;
	if (!memmgr || MEMORY_DIR_OUT == memmgr->dir)
		return NULL;

	spin_lock(&memmgr->spin);
	if (list_empty(&memmgr->free_list)) {
		spin_unlock(&memmgr->spin);
		return NULL;
	}

	_abuf = list_first_entry(&memmgr->free_list, struct _audio_buffer, node);
	list_move_tail(&_abuf->node, &memmgr->rendered_list);
	spin_unlock(&memmgr->spin);

	return &_abuf->buffer;
}

struct audio_buffer *spacemit_snd_get_first_rendered_buffer(struct audio_memory_manager *memmgr)
{
	struct _audio_buffer *_abuf;

	if (!memmgr)
		return NULL;

	spin_lock(&memmgr->spin);
	if (list_empty(&memmgr->rendered_list))
		_abuf = NULL;
	else
		_abuf = list_first_entry(&memmgr->rendered_list, struct _audio_buffer, node);
	spin_unlock(&memmgr->spin);

	return _abuf ? &_abuf->buffer : NULL;
}

int spacemit_snd_finish_one_buffer(struct audio_memory_manager *memmgr, struct audio_buffer *abuf)
{
	struct _audio_buffer *_abuf = (struct _audio_buffer *)abuf;

	if (!memmgr || !_abuf)
		return -EINVAL;

	spin_lock(&memmgr->spin);
	if (memmgr->dir == MEMORY_DIR_OUT) {
		_abuf->buffer.filled = 0;
	} else {
		_abuf->buffer.offset = 0;
	}
	list_move_tail(&_abuf->node, &memmgr->ready_list);
	spin_unlock(&memmgr->spin);

	wake_up(&memmgr->w);

	return 0;
}

int spacemit_snd_free_memory(struct audio_memory_manager *memmgr)
{
	if (!memmgr)
		return -EINVAL;

	kfree(memmgr->_abuf);

	if (memmgr->vaddr)
		adsp_ipc_shm_free(memmgr->vaddr, memmgr->size);

	kfree(memmgr);

	return 0;
}

void *spacemit_snd_get_memory_vaddr(struct audio_memory_manager *memmgr)
{
	if (!memmgr)
		return NULL;

	return memmgr->vaddr;
}

int spacemit_snd_get_memory_paddr(struct audio_memory_manager *memmgr, dma_addr_t *paddr)
{
	if (!memmgr || !paddr)
		return -EINVAL;

	*paddr = memmgr->paddr;
	return 0;
}
