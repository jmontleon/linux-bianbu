// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2022 SPACEMIT Micro Limited
 */

#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/freezer.h>

#include "spacemit-snd-ipc.h"

#define SPACEMIT_SND_IPC_MSGS_MAX  32

extern void adsp_monitor_err_handler(const char * id, int errno);

static int _alloc_msgs_pool(struct spacemit_snd_ipc *ipc)
{
	int i;

	ipc->msgs = kzalloc(sizeof(struct spacemit_snd_ipc_msg) * SPACEMIT_SND_IPC_MSGS_MAX, GFP_KERNEL);
	if (!ipc->msgs)
		return -ENOMEM;

	for (i = 0; i < SPACEMIT_SND_IPC_MSGS_MAX; i++) {
		ipc->msgs[i].tx_data = kzalloc(ipc->tx_data_max_size, GFP_KERNEL);
		if (ipc->msgs[i].tx_data == NULL)
			goto fail;

		ipc->msgs[i].rx_data = kzalloc(ipc->rx_data_max_size, GFP_KERNEL);
		if (ipc->msgs[i].rx_data == NULL) {
			kfree(ipc->msgs[i].tx_data);
			goto fail;
		}

		init_waitqueue_head(&ipc->msgs[i].waitq);
		list_add(&ipc->msgs[i].list, &ipc->empty_list);
	}

	return 0;

fail:
	while (i > 0) {
		kfree(ipc->msgs[i-1].tx_data);
		kfree(ipc->msgs[i-1].rx_data);
		--i;
	}
	kfree(ipc->msgs);

	return -ENOMEM;
}

static void _free_msgs_pool(struct spacemit_snd_ipc *ipc)
{
	if (ipc->msgs) {
		int i;
		for (i = 0; i < SPACEMIT_SND_IPC_MSGS_MAX; i++) {
			kfree(ipc->msgs[i].tx_data);
			kfree(ipc->msgs[i].rx_data);
		}
		kfree(ipc->msgs);

		INIT_LIST_HEAD(&ipc->tx_list);
		INIT_LIST_HEAD(&ipc->rx_list);
		INIT_LIST_HEAD(&ipc->empty_list);
	}
}

static struct spacemit_snd_ipc_msg *_get_msg_from_pool(struct spacemit_snd_ipc *ipc)
{
	struct spacemit_snd_ipc_msg *msg = NULL;

	if (!list_empty(&ipc->empty_list)) {
		msg = list_first_entry(&ipc->empty_list, struct spacemit_snd_ipc_msg, list);
		list_del(&msg->list);
	}

	return msg;
}

static int _spacemit_snd_ipc_wait_tx_done(struct spacemit_snd_ipc *ipc, struct spacemit_snd_ipc_msg *msg,
	void *rx_data, uint32_t wait)
{
	int ret;

	ret = wait_event_timeout(msg->waitq, msg->complete,
		msecs_to_jiffies(wait));

	spin_lock(&ipc->spin);
	if (ret == 0) {
		ret = -ETIMEDOUT;
	} else {
		if((msg->rx_size) && (rx_data))
			memcpy(rx_data, msg->rx_data, msg->rx_size);
		ret = msg->errno;
	}

	if (ret) {
		pr_info("%s: warning ret(%d)\n", __func__, ret);
		if (ret == -ETIMEDOUT)
			adsp_monitor_err_handler("audio", ret);
	}

	list_del(&msg->list);
	list_add_tail(&msg->list, &ipc->empty_list);
	spin_unlock(&ipc->spin);

	return ret;
}

static int _spacemit_snd_ipc_send_msg(struct spacemit_snd_ipc *ipc, void *owner, msg_cbf_t cbf, void *arg, \
	uint32_t header, void *tx_data, size_t tx_size, void *rx_data, size_t rx_size, uint32_t wait, uint32_t needs_ack)
{
	struct spacemit_snd_ipc_msg *msg;

	if (tx_size + sizeof(header) > ipc->tx_data_max_size ||
			rx_size + sizeof(header) > ipc->rx_data_max_size)
		return -EINVAL;

	spin_lock(&ipc->spin);
	msg = _get_msg_from_pool(ipc);
	if (msg == NULL) {
		spin_unlock(&ipc->spin);
		pr_err("failed to get msg from pool\n");
		return -EBUSY;
	}
	spin_unlock(&ipc->spin);

	msg->header = header;
	msg->owner = owner;
	msg->tx_size = sizeof(header) + tx_size;
	msg->rx_size = rx_size;
	msg->wait = wait;
	msg->needs_ack = false;
	if (wait)
		msg->needs_ack = true;
	else {
		if (needs_ack)
			msg->needs_ack = true;
	}
	msg->errno = 0;
	msg->complete = false;
	msg->cbf = cbf;
	msg->arg = arg;
	memcpy(msg->tx_data, &header, sizeof(header));
	memcpy(msg->tx_data + sizeof(header), tx_data, tx_size);

	spin_lock(&ipc->spin);
	list_add_tail(&msg->list, &ipc->tx_list);
	spin_unlock(&ipc->spin);

	wake_up_process(ipc->tx_thread);

	return wait ? _spacemit_snd_ipc_wait_tx_done(ipc, msg, rx_data, wait) : 0;
}

int spacemit_snd_ipc_send_msg_wait(struct spacemit_snd_ipc *ipc, void *owner, uint32_t header, \
	void *tx_data, size_t tx_size, void *rx_data, size_t rx_size, uint32_t wait)
{
	return _spacemit_snd_ipc_send_msg(ipc, owner, NULL, NULL, header, tx_data, tx_size, rx_data, rx_size, wait, true);
}

int spacemit_snd_ipc_send_msg(struct spacemit_snd_ipc *ipc, void *owner, msg_cbf_t cbf, void *arg, uint32_t header, \
	void *tx_data, size_t tx_size, uint32_t needs_ack)
{
	return _spacemit_snd_ipc_send_msg(ipc, owner, cbf, arg, header, tx_data, tx_size, NULL, 0, 0, needs_ack);
}

void spacemit_snd_ipc_drop_msgs(struct spacemit_snd_ipc *ipc, void *owner)
{
	struct spacemit_snd_ipc_msg *msg, *msg_next;

	spin_lock(&ipc->spin);
	list_for_each_entry_safe(msg, msg_next, &ipc->rx_list, list) {
		if (msg->owner == owner)
			list_move_tail(&msg->list, &ipc->empty_list);
	}
	spin_unlock(&ipc->spin);
}

static int _spacemit_snd_ipc_tx_msg(void *arg)
{
	struct spacemit_snd_ipc_msg *msg;
	struct spacemit_snd_ipc *ipc = (struct spacemit_snd_ipc *)arg;
	int ret;
	DEFINE_WAIT(wq);

	struct sched_param param = {.sched_priority = MAX_RT_PRIO - 1 };
	ret = sched_setscheduler(current, SCHED_FIFO, &param);
	if (ret < 0)
		pr_err("%s, ret = %d\n", __func__, ret);

	set_freezable();

	while (!kthread_should_stop()) {
		set_current_state(TASK_INTERRUPTIBLE);

		spin_lock(&ipc->spin);
		if (!list_empty(&ipc->tx_list)) {
			set_current_state(TASK_RUNNING);
			msg = list_first_entry(&ipc->tx_list, struct spacemit_snd_ipc_msg, list);
			if (msg->needs_ack)
				list_move_tail(&msg->list, &ipc->rx_list);
			else
				list_move_tail(&msg->list, &ipc->empty_list);

			spin_unlock(&ipc->spin);
			if (ipc->ops && ipc->ops->tx_data)
				ipc->ops->tx_data(ipc, msg->tx_data, msg->tx_size);
			continue;
		}
		spin_unlock(&ipc->spin);
		if (!freezing(current))
			schedule();
		try_to_freeze();
	}

	pr_err("exit %s\n", __func__);

	return 0;
}

static struct spacemit_snd_ipc_msg *spacemit_snd_ipc_reply_find_msg(struct spacemit_snd_ipc *ipc, uint32_t mask, uint32_t token)
{
	struct spacemit_snd_ipc_msg *msg;

	spin_lock(&ipc->spin);
	if (list_empty(&ipc->rx_list)) {
		spin_unlock(&ipc->spin);
		return NULL;
	}

	list_for_each_entry(msg, &ipc->rx_list, list) {
		if (token == (msg->header & mask)) {
			spin_unlock(&ipc->spin);
			return msg;
		}
	}
	spin_unlock(&ipc->spin);

	return NULL;
}

static void spacemit_snd_ipc_reply_msg_complete(struct spacemit_snd_ipc *ipc, struct spacemit_snd_ipc_msg *msg)
{
	msg->complete = true;
	if (!msg->wait) {
		spin_lock(&ipc->spin);
		list_move_tail(&msg->list, &ipc->empty_list);
		spin_unlock(&ipc->spin);
		return;
	}
	wake_up(&msg->waitq);
}

void spacemit_snd_ipc_handle_reply_msg(struct spacemit_snd_ipc *ipc, uint32_t mask, void *body, uint32_t token)
{
	uint32_t err_no;
	struct spacemit_snd_ipc_msg *ipc_msg;

	err_no = *(uint32_t *)(body);
	ipc_msg = spacemit_snd_ipc_reply_find_msg(ipc, mask, token);
	if (ipc_msg) {
		ipc_msg->errno = err_no;
		if (ipc_msg->rx_size)
			memcpy(ipc_msg->rx_data, body + sizeof(err_no), ipc_msg->rx_size);

		if (ipc_msg->cbf)
			ipc_msg->cbf(ipc_msg->arg, ipc_msg->rx_data);

		spacemit_snd_ipc_reply_msg_complete(ipc, ipc_msg);
	} else {
		pr_warn("cannot find message by token=0x%x\n", token);
	}
}

int spacemit_snd_ipc_open(struct spacemit_snd_ipc *ipc, struct spacemit_snd_ipc_ops *ops)
{
	int ret;

	spin_lock_init(&ipc->spin);

	INIT_LIST_HEAD(&ipc->tx_list);
	INIT_LIST_HEAD(&ipc->rx_list);
	INIT_LIST_HEAD(&ipc->empty_list);

	ipc->ops = ops;

	ret = _alloc_msgs_pool(ipc);
	if (ret < 0)
		return -ENOMEM;

	ipc->tx_thread = kthread_run(_spacemit_snd_ipc_tx_msg, ipc, ipc->name);
	if (IS_ERR(ipc->tx_thread)) {
		ret = PTR_ERR(ipc->tx_thread);
		_free_msgs_pool(ipc);
		return ret;
	}

	return 0;
}

void spacemit_snd_ipc_close(struct spacemit_snd_ipc *ipc)
{
	if (ipc->tx_thread)
		kthread_stop(ipc->tx_thread);

	_free_msgs_pool(ipc);
}

