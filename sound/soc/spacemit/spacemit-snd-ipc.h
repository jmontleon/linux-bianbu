// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2022 SPACEMIT Micro Limited
 */

#ifndef _SPACEMIT_SND_IPC_H
#define _SPACEMIT_SND_IPC_H

#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/list.h>
#include <uapi/linux/sched/types.h>

struct spacemit_snd_ipc;
struct spacemit_snd_ipc_ops {
	int (*tx_data)(struct spacemit_snd_ipc *ipc, void *data, size_t len);
};

struct spacemit_snd_ipc;
struct spacemit_snd_ipc_msg;
typedef void (*msg_cbf_t)(void *arg, void *rx_data);
struct spacemit_snd_ipc_msg {
	uint32_t header;
	struct list_head list;
	void *owner;
	void *tx_data;
	size_t tx_size;
	void *rx_data;
	size_t rx_size;
	wait_queue_head_t waitq;
	msg_cbf_t cbf;
	void *arg;

	bool complete;
	uint32_t wait;
	bool needs_ack;
	int errno;
};

struct spacemit_snd_ipc {
	const char *name;

	spinlock_t spin;
	struct list_head tx_list;
	struct list_head rx_list;
	struct list_head empty_list;

	struct task_struct *tx_thread;

	struct spacemit_snd_ipc_msg *msgs;
	int tx_data_max_size;
	int rx_data_max_size;
	struct spacemit_snd_ipc_ops *ops;
};

int spacemit_snd_ipc_open(struct spacemit_snd_ipc *ipc, struct spacemit_snd_ipc_ops *ops);
void spacemit_snd_ipc_close(struct spacemit_snd_ipc *ipc);
int spacemit_snd_ipc_send_msg(struct spacemit_snd_ipc *ipc, void *owner, msg_cbf_t cbf, void *arg, \
	uint32_t header, void *tx_data, size_t tx_size, uint32_t needs_ack);
int spacemit_snd_ipc_send_msg_wait(struct spacemit_snd_ipc *ipc, void *owner, uint32_t header, void *tx_data, \
	size_t tx_size, void *rx_data, size_t rx_size, uint32_t wait);
void spacemit_snd_ipc_drop_msgs(struct spacemit_snd_ipc *ipc, void *owner);
void spacemit_snd_ipc_handle_reply_msg(struct spacemit_snd_ipc *ipc, uint32_t mask, void *body, uint32_t token);

#endif
