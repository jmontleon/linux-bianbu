// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2022 SPACEMIT Micro Limited
 */

#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/freezer.h>
#include <linux/delay.h>

#include "spacemit-snd-adsp.h"
#include "spacemit-snd-ipc.h"
#include "spacemit-snd-msgs.h"

#define SPACEMIT_SND_IPC_TIMEOUT_MSECS 600

struct spacemit_snd_adsp_event_handler_ {
	struct spacemit_snd_adsp_event_handler *ev_handler;
	struct list_head node;
};

struct spacemit_snd_adsp_client {
	const char *name;
	atomic_t seq_no;
	struct spacemit_snd_ipc ipc;
	struct task_struct *rx_thread;
	spinlock_t spinlock;
	void *data_mem;
	adsp_channel_t *ch;
};

static inline uint32_t CMD2HEADER(struct spacemit_snd_adsp_client *adsp_client, uint32_t cmd)
{
	return cmd | IPC_MSG_SEQ_NO(atomic_inc_return(&adsp_client->seq_no));
}

int spacemit_snd_adsp_stream_alloc(struct spacemit_snd_adsp_client *adsp_client, struct spacemit_snd_adsp_stream *s)
{
	int ret;

	struct spacemit_snd_ipc_stream_alloc stream_alloc;
	struct spacemit_snd_ipc *ipc = &adsp_client->ipc;

	pr_debug("func: %s\n", __func__);
	stream_alloc.stream_type = s->type;

	stream_alloc.bits = s->bits;
	stream_alloc.channels = s->channels;
	stream_alloc.sample_rate = s->sample_rate;
	stream_alloc.format = s->format;
	stream_alloc.bit_rate = s->bit_rate;
	stream_alloc.hw_dev = s->hw_dev;
	stream_alloc.dma_enhancement = s->dma_enhancement;
	ret = spacemit_snd_ipc_send_msg_wait(ipc, s, CMD2HEADER(adsp_client, IPC_STREAM_MSG_ALLOC),
		&stream_alloc, sizeof(stream_alloc), NULL, 0, SPACEMIT_SND_IPC_TIMEOUT_MSECS);
	if (ret)
		return ret;
	pr_debug("adsp stream id: 0x%x\n", s->type);

	return 0;
}

int spacemit_snd_adsp_stream_free(struct spacemit_snd_adsp_client *adsp_client, struct spacemit_snd_adsp_stream *s)
{
	int ret = 0;

	struct spacemit_snd_ipc_stream_free stream_free;
	struct spacemit_snd_ipc *ipc = &adsp_client->ipc;

	pr_debug("func: %s\n", __func__);
	stream_free.stream_type = s->type;
	ret = spacemit_snd_ipc_send_msg_wait(ipc, s, CMD2HEADER(adsp_client, IPC_STREAM_MSG_FREE),
		&stream_free, sizeof(stream_free), NULL, 0, SPACEMIT_SND_IPC_TIMEOUT_MSECS);

	spin_lock(&adsp_client->spinlock);
	spacemit_snd_ipc_drop_msgs(&adsp_client->ipc, s);
	spin_unlock(&adsp_client->spinlock);

	return ret;
}

int spacemit_snd_adsp_stream_resume(struct spacemit_snd_adsp_client *adsp_client, struct spacemit_snd_adsp_stream *s)
{
	struct spacemit_snd_ipc_stream_resume stream_resume;
	struct spacemit_snd_ipc *ipc = &adsp_client->ipc;

	pr_debug("func: %s\n", __func__);
	stream_resume.stream_type = s->type;
	return spacemit_snd_ipc_send_msg(ipc, s, NULL, NULL, CMD2HEADER(adsp_client, IPC_STREAM_MSG_RESUME),
		&stream_resume, sizeof(struct spacemit_snd_ipc_stream_resume), IPC_MSG_NEEDS_ACK(IPC_STREAM_MSG_RESUME));
}

int spacemit_snd_adsp_stream_pause(struct spacemit_snd_adsp_client *adsp_client, struct spacemit_snd_adsp_stream *s)
{
	struct spacemit_snd_ipc_stream_pause stream_pause;
	struct spacemit_snd_ipc *ipc = &adsp_client->ipc;

	pr_debug("func: %s\n", __func__);
	stream_pause.stream_type = s->type;
	return spacemit_snd_ipc_send_msg(ipc, s, NULL, NULL, CMD2HEADER(adsp_client, IPC_STREAM_MSG_PAUSE),
		&stream_pause, sizeof(struct spacemit_snd_ipc_stream_pause), IPC_MSG_NEEDS_ACK(IPC_STREAM_MSG_PAUSE));

}

int spacemit_snd_adsp_stream_eos(struct spacemit_snd_adsp_client *adsp_client, msg_cbf_t callback, struct spacemit_snd_adsp_stream *s)
{
	struct spacemit_snd_ipc_stream_eos stream_eos;
	struct spacemit_snd_ipc *ipc = &adsp_client->ipc;

	pr_debug("func: %s\n", __func__);
	stream_eos.stream_type = s->type;
	return spacemit_snd_ipc_send_msg(ipc, s, callback, s, CMD2HEADER(adsp_client, IPC_STREAM_MSG_EOS),
		&stream_eos, sizeof(struct spacemit_snd_ipc_stream_eos), !!(callback != NULL));
}

int spacemit_snd_adsp_stream_flush(struct spacemit_snd_adsp_client *adsp_client, struct spacemit_snd_adsp_stream *s)
{
	int ret;

	struct spacemit_snd_ipc_stream_flush stream_flush;
	struct spacemit_snd_ipc *ipc = &adsp_client->ipc;

	pr_debug("func: %s\n", __func__);
	stream_flush.stream_type = s->type;

	ret = spacemit_snd_ipc_send_msg_wait(ipc, s, CMD2HEADER(adsp_client, IPC_STREAM_MSG_FLUSH),
		&stream_flush, sizeof(stream_flush), NULL, 0, SPACEMIT_SND_IPC_TIMEOUT_MSECS);

	spacemit_snd_ipc_drop_msgs(ipc, s);

	return ret;
}

int spacemit_snd_adsp_stream_write_data(struct spacemit_snd_adsp_client *adsp_client, msg_cbf_t callback, \
	struct spacemit_snd_adsp_stream *s, struct audio_buffer *abuf)
{
	struct spacemit_snd_ipc_stream_write_data stream_write_data;
	struct spacemit_snd_ipc *ipc = &adsp_client->ipc;

	pr_debug("func: %s\n", __func__);
	stream_write_data.paddr = abuf->paddr;
	stream_write_data.filled = abuf->filled;
	stream_write_data.stream_type = s->type;

	return spacemit_snd_ipc_send_msg(ipc, s, callback, s, CMD2HEADER(adsp_client, IPC_STREAM_MSG_WRITE_DATA),
		&stream_write_data, sizeof(stream_write_data), IPC_MSG_NEEDS_ACK(IPC_STREAM_MSG_WRITE_DATA));
}


int spacemit_snd_adsp_stream_read_data(struct spacemit_snd_adsp_client *adsp_client, msg_cbf_t callback, \
	struct spacemit_snd_adsp_stream *s, struct audio_buffer *abuf)
{
	struct spacemit_snd_ipc_stream_read_data stream_read_data;
	struct spacemit_snd_ipc *ipc = &adsp_client->ipc;

	pr_debug("func: %s\n", __func__);
	stream_read_data.paddr = abuf->paddr;
	stream_read_data.required = abuf->size;
	stream_read_data.stream_type = s->type;

	return spacemit_snd_ipc_send_msg(ipc, s, callback, s, CMD2HEADER(adsp_client, IPC_STREAM_MSG_READ_DATA),
		&stream_read_data, sizeof(stream_read_data), IPC_MSG_NEEDS_ACK(IPC_STREAM_MSG_READ_DATA));
}

int spacemit_snd_adsp_stream_set_volume(struct spacemit_snd_adsp_client *adsp_client, struct spacemit_snd_adsp_stream *s, uint32_t lvol, uint32_t rvol, uint32_t curve_duration)
{
	struct spacemit_snd_ipc_stream_set_volume stream_set_volume;
	struct spacemit_snd_ipc *ipc = &adsp_client->ipc;

	pr_debug("func: %s, (L:%d, R:%d)\n", __func__, lvol, rvol);
	stream_set_volume.stream_type = s->type;
	stream_set_volume.left_volume = lvol;
	stream_set_volume.right_volume = rvol;
	stream_set_volume.curve_duration = curve_duration;
	return spacemit_snd_ipc_send_msg(ipc, s, NULL, NULL, CMD2HEADER(adsp_client, IPC_STREAM_MSG_SET_VOLUME),
		&stream_set_volume, sizeof(stream_set_volume), IPC_MSG_NEEDS_ACK(IPC_STREAM_MSG_SET_VOLUME));

}

int spacemit_snd_adsp_stream_set_mic_mute(struct spacemit_snd_adsp_client *adsp_client,
	struct spacemit_snd_adsp_stream *s, uint32_t mute, uint32_t curve_duration)
{
	struct spacemit_snd_ipc_stream_set_mic_mute stream_set_mute;
	struct spacemit_snd_ipc *ipc = &adsp_client->ipc;

	pr_debug("func: %s, %d\n", __func__, mute);
	stream_set_mute.stream_type = s->type;
	stream_set_mute.mute = mute;
	stream_set_mute.curve_duration = curve_duration;

	return spacemit_snd_ipc_send_msg(ipc, s, NULL, NULL,
		CMD2HEADER(adsp_client, IPC_STREAM_MSG_SET_MIC_MUTE),
		&stream_set_mute, sizeof(stream_set_mute),
		IPC_MSG_NEEDS_ACK(IPC_STREAM_MSG_SET_MIC_MUTE));
}

int spacemit_snd_adsp_stream_set_hw_dev(struct spacemit_snd_adsp_client *adsp_client,
	struct spacemit_snd_adsp_stream *s, uint32_t device)
{
	struct spacemit_snd_ipc_stream_set_hw_dev stream_set_dev;
	struct spacemit_snd_ipc *ipc = &adsp_client->ipc;

	pr_debug("func: %s, %d\n", __func__, device);
	stream_set_dev.stream_type = s->type;
	stream_set_dev.device = device;
	return spacemit_snd_ipc_send_msg(ipc, s, NULL, NULL,
		CMD2HEADER(adsp_client, IPC_STREAM_MSG_SET_HW_DEV),
		&stream_set_dev, sizeof(stream_set_dev),
		IPC_MSG_NEEDS_ACK(IPC_STREAM_MSG_SET_HW_DEV));

}

int spacemit_snd_adsp_stream_set_route(struct spacemit_snd_adsp_client *adsp_client,
	uint32_t id, uint32_t dir, uint32_t backend_id)
{
	struct spacemit_snd_ipc_stream_set_route stream_set_route;
	struct spacemit_snd_ipc *ipc = &adsp_client->ipc;

	pr_debug("func: %s, id=%d, dir =%d. back:%d\n", __func__, id, dir, backend_id);
	stream_set_route.stream_type =  STREAM_TYPE(dir, id);/*dir playback capture*/
	stream_set_route.backend_id = backend_id;
	return spacemit_snd_ipc_send_msg(ipc, NULL, NULL, NULL, CMD2HEADER(adsp_client, IPC_STREAM_MSG_SET_ROUTE),
		&stream_set_route, sizeof(stream_set_route), IPC_MSG_NEEDS_ACK(IPC_STREAM_MSG_SET_ROUTE));

}

int spacemit_snd_adsp_codec_set_device(uint32_t dev_id, uint32_t enable)
{
	struct spacemit_snd_adsp_client *adsp_client = spacemit_snd_get_adsp_client(0, NULL);

	struct spacemit_snd_ipc_codec_set_device set_codec_device;
	struct spacemit_snd_ipc *ipc = &adsp_client->ipc;

	pr_debug("func: %s, dev_id=%d, enable=%d\n", __func__, dev_id, enable);
	set_codec_device.dev_id = dev_id;
	set_codec_device.enable = enable;
	return spacemit_snd_ipc_send_msg_wait(ipc, NULL, CMD2HEADER(adsp_client, IPC_GLOBAL_MSG_CODEC_SET_DEVICE),
		&set_codec_device, sizeof(set_codec_device), NULL, 0, SPACEMIT_SND_IPC_TIMEOUT_MSECS);
}
EXPORT_SYMBOL(spacemit_snd_adsp_codec_set_device);

int spacemit_snd_adsp_set_reg(uint32_t reg_adr, uint32_t mask, uint32_t val)
{
	struct spacemit_snd_adsp_client *adsp_client = spacemit_snd_get_adsp_client(0, NULL);

	struct spacemit_snd_ipc_audio_reg audio_reg;
	struct spacemit_snd_ipc *ipc = &adsp_client->ipc;

	audio_reg.reg_adr = reg_adr;
	audio_reg.mask = mask;
	audio_reg.val = val;

	return spacemit_snd_ipc_send_msg_wait(ipc, NULL,
		CMD2HEADER(adsp_client, IPC_GLOBAL_MSG_AUDIO_SET_REG),
		&audio_reg, sizeof(audio_reg),
		NULL, 0, SPACEMIT_SND_IPC_TIMEOUT_MSECS);
}
EXPORT_SYMBOL(spacemit_snd_adsp_set_reg);

int spacemit_snd_adsp_get_reg(uint32_t reg_adr, uint32_t mask, uint32_t *val)
{
	struct spacemit_snd_adsp_client *adsp_client = spacemit_snd_get_adsp_client(0, NULL);
	struct spacemit_snd_ipc_audio_reg audio_reg;
	struct spacemit_snd_ipc *ipc = &adsp_client->ipc;
	int ret;

	audio_reg.reg_adr = reg_adr;
	audio_reg.mask = mask;
	audio_reg.val = 0;
	ret = spacemit_snd_ipc_send_msg_wait(ipc, NULL,
		CMD2HEADER(adsp_client, IPC_GLOBAL_MSG_AUDIO_GET_REG),
		&audio_reg, sizeof(audio_reg),// tx size
		&audio_reg, sizeof(audio_reg),// rx size
		SPACEMIT_SND_IPC_TIMEOUT_MSECS);

	*val = audio_reg.val;
	pr_debug("func: %s, reg_adr=0x%x,mask=0x%x, val=0x%x\n", __func__, audio_reg.reg_adr, audio_reg.mask, audio_reg.val);

	return ret;
}
EXPORT_SYMBOL(spacemit_snd_adsp_get_reg);

int spacemit_snd_adsp_stream_set_rec_mode(struct spacemit_snd_adsp_client *adsp_client,
	struct spacemit_snd_adsp_stream *s, uint32_t mode)
{
	struct spacemit_snd_ipc_stream_set_rec_mode stream_rec_mode;
	struct spacemit_snd_ipc *ipc = &adsp_client->ipc;

	pr_debug("func: %s, record_mode:%d\n", __func__, mode);
	stream_rec_mode.stream_type =  s->type;
	stream_rec_mode.mode = mode;
	return spacemit_snd_ipc_send_msg(ipc, NULL, NULL, NULL, CMD2HEADER(adsp_client, IPC_STREAM_MSG_SET_RECORD_MODE),
		&stream_rec_mode, sizeof(stream_rec_mode), IPC_MSG_NEEDS_ACK(IPC_STREAM_MSG_SET_RECORD_MODE));

}

static DEFINE_MUTEX(ev_handlers_mtx);
static LIST_HEAD(ev_handlers);

static int _spacemit_snd_ipc_rx_data(void *arg)
{
	uint32_t id;
	uint32_t header;
	uint32_t dir;
	uint32_t token;
	uint32_t err_no;
	int32_t ret;
	void *body;
	DEFINE_WAIT(wait);

	struct spacemit_snd_adsp_event_handler_ *handler_;
	struct spacemit_snd_adsp_client *adsp_client = (struct spacemit_snd_adsp_client *)arg;
	struct spacemit_snd_ipc *ipc = &adsp_client->ipc;
	adsp_channel_t *ipc_ch = adsp_client->ch;
	wait_queue_head_t *wq = adsp_ipc_get_rxwaitq(ipc_ch);
	struct sched_param param = {.sched_priority = MAX_RT_PRIO - 1 };

	ret = sched_setscheduler(current, SCHED_FIFO, &param);
	if (ret < 0)
		pr_err("%s, ret = %d\n", __func__, ret);

	set_freezable();

	do {
		wq = adsp_ipc_get_rxwaitq(ipc_ch);
		if (!wq) {
			msleep(400);
			continue;
		}
		try_to_freeze();
		prepare_to_wait(wq, &wait, TASK_INTERRUPTIBLE);
		if (adsp_ipc_rx_available(ipc_ch) <= 0)
			schedule();
		finish_wait(wq, &wait);

		if ((ret = adsp_ipc_rx(ipc_ch, adsp_client->data_mem, 1, 0)) != 1) {
			if (ret < 0)
				pr_info("_spacemit_snd_ipc_rx_data warning rx, ret=%d!\n", ret);
			continue;
		}
		header = *(uint32_t *)adsp_client->data_mem;
		dir = header & IPC_MSG_DIR_MASK;
		if (IPC_MSG_DIR_ADSP2AP_REPLY == dir) {
			token = header & ~IPC_MSG_DIR_MASK;
			err_no = *(uint32_t *)(adsp_client->data_mem + sizeof(header));
			spin_lock(&adsp_client->spinlock);
			body = adsp_client->data_mem + sizeof(header);
			spacemit_snd_ipc_handle_reply_msg(ipc, ~IPC_MSG_DIR_MASK, body, token);

			spin_unlock(&adsp_client->spinlock);
		} else if (IPC_MSG_DIR_ADSP2AP_NOTIFICATION == dir) {
			id = header & ~IPC_MSG_DIR_MASK;
			body = adsp_client->data_mem + sizeof(header);
			mutex_lock(&ev_handlers_mtx);
			list_for_each_entry(handler_, &ev_handlers, node) {
				if (handler_->ev_handler && handler_->ev_handler->handle_event) {
					handler_->ev_handler->handle_event(handler_->ev_handler, id, body);
				}
			}
			mutex_unlock(&ev_handlers_mtx);
		}
	} while (!kthread_should_stop());

	pr_err("%s, exit \n", __func__);

	return 0;
}

static int __spacemit_snd_ipc_tx_data(struct spacemit_snd_ipc *ipc, void *tx_data, size_t len)
{
	/*FIXME*/
	unsigned char tbuf[IPC_MSG_BYTES_MAX];
	struct spacemit_snd_adsp_client *adsp;
	int ret = 0;

	if (len > IPC_MSG_BYTES_MAX) {
		pr_err("__spacemit_snd_ipc_tx_data tx error len:%ld\n", len);
		return -EINVAL;
	}

	adsp = container_of(ipc, struct spacemit_snd_adsp_client, ipc);

	memset(tbuf, 0, IPC_MSG_BYTES_MAX);
	memcpy(tbuf, tx_data, len);
	ret = adsp_ipc_tx(adsp->ch, tbuf, 1, 0);
	if (ret != 1) {
		pr_err("__spacemit_snd_ipc_tx_data tx err, ret=%d\n", ret);
		if (ret == 0)
			return -EAGAIN;
		return ret;
	}

	return 0;
}

static struct spacemit_snd_ipc_ops _spacemit_snd_ipc_ops = {
	.tx_data = __spacemit_snd_ipc_tx_data,
};

/*
 * All substreams (pcm/compress) share the same adsp client (and also IPC channel).
 * If different adsp clients are needed for separate substreams, please rewrite the following function.
 */
static struct spacemit_snd_adsp_client *_global_adsp_client = NULL;
static DEFINE_MUTEX(_adsp_client_mtx);

static int spacemit_snd_adsp_client_init(struct spacemit_snd_adsp_client *adsp_client)
{
	int ret = 0;
	pr_debug("enter %s\n", __func__);

	adsp_client->ch = adsp_ipc_open(ADSP_CHANL_AUDIO_CTRL, ADSP_TYPE_BIDIR, IPC_MSG_BYTES_MAX, 64);
	if (!adsp_client->ch) {
		pr_err("snd adsp ipc open failed\n");
		goto fail;
	}

	atomic_set(&adsp_client->seq_no, 0);
	spin_lock_init(&adsp_client->spinlock);

	adsp_client->ipc.tx_data_max_size = IPC_MSG_BYTES_MAX;
	adsp_client->ipc.rx_data_max_size = IPC_MSG_BYTES_MAX;

	adsp_client->data_mem = kzalloc(IPC_MSG_BYTES_MAX, GFP_KERNEL);
	if (!adsp_client->data_mem) {
		ret = -ENOMEM;
		goto fail0;
	}

	adsp_client->ipc.name = adsp_client->name;
	ret = spacemit_snd_ipc_open(&adsp_client->ipc, &_spacemit_snd_ipc_ops);
	if (ret)
		goto fail1;

	adsp_client->rx_thread = kthread_run(_spacemit_snd_ipc_rx_data, \
					(void *)adsp_client, adsp_client->name);
	if (IS_ERR(adsp_client->rx_thread)) {
		long err = PTR_ERR(adsp_client->rx_thread);
		pr_debug("failed to start sound rx data thread: %ld\n", err);
		goto fail2;
	}

	pr_info("adsp snd ipc client inited\n");

	return 0;

fail2:
	spacemit_snd_ipc_close(&adsp_client->ipc);
fail1:
	kfree(adsp_client->data_mem);
fail0:
	adsp_ipc_close(adsp_client->ch);
fail:
	return ret;
}

struct spacemit_snd_adsp_client *spacemit_snd_get_adsp_client(int id, char *name)
{
	struct spacemit_snd_adsp_client *adsp_client;

	mutex_lock(&_adsp_client_mtx);
	if (NULL == _global_adsp_client) {
		_global_adsp_client = kzalloc(sizeof(struct spacemit_snd_adsp_client), GFP_KERNEL);
		if (_global_adsp_client) {
			_global_adsp_client->name = name;
			if (spacemit_snd_adsp_client_init(_global_adsp_client) < 0) {
				pr_err("failed to init adsp snd ipc client\n");
				kfree(_global_adsp_client);
				_global_adsp_client = NULL;
			}
		}
	}
	adsp_client = _global_adsp_client;
	mutex_unlock(&_adsp_client_mtx);

	return adsp_client;
}
EXPORT_SYMBOL(spacemit_snd_get_adsp_client);

void spacemit_snd_adsp_init(const char *name)
{
	spacemit_snd_get_adsp_client(0, (char *)name);
}
EXPORT_SYMBOL(spacemit_snd_adsp_init);

int spacemit_snd_adsp_register_event_handler(struct spacemit_snd_adsp_event_handler *handler)
{
	struct spacemit_snd_adsp_event_handler_ *handler_;

	handler_ = kmalloc(sizeof(*handler_), GFP_KERNEL);
	if (!handler_)
		return -ENOMEM;

	handler_->ev_handler = handler;

	mutex_lock(&ev_handlers_mtx);
	list_add_tail(&handler_->node, &ev_handlers);
	mutex_unlock(&ev_handlers_mtx);

	return 0;
}

void spacemit_snd_adsp_unregister_event_handler(struct spacemit_snd_adsp_event_handler *handler)
{
	struct spacemit_snd_adsp_event_handler_ *handler_, *n_;

	mutex_lock(&ev_handlers_mtx);
	list_for_each_entry_safe(handler_, n_, &ev_handlers, node) {
		if (handler_->ev_handler == handler) {
			list_del(&handler_->node);
			kfree(handler_);
			break;
		}
	}
	mutex_unlock(&ev_handlers_mtx);
}
