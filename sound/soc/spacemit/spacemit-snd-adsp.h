// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2022 SPACEMIT Micro Limited
 */

#ifndef _SPACEMIT_SND_ADSP_H
#define _SPACEMIT_SND_ADSP_H

#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/kref.h>
#include <soc/spacemit/adsp_ipc.h>

#include "spacemit-snd-ipc.h"
#include "spacemit-snd-memory.h"

struct spacemit_snd_adsp_stream {
	uint32_t type;
	uint32_t format;
	uint32_t sample_rate;
	uint32_t bits;
	uint32_t channels;
	uint32_t bit_rate;
	uint32_t hw_dev;
	dma_addr_t dma_enhancement;
};

struct spacemit_snd_adsp_event_handler {
	void (*handle_event)(struct spacemit_snd_adsp_event_handler *, uint32_t ev, void *);
};

struct spacemit_snd_adsp_client;

int spacemit_snd_adsp_stream_alloc(struct spacemit_snd_adsp_client *adsp_client, struct spacemit_snd_adsp_stream *s);
int spacemit_snd_adsp_stream_free(struct spacemit_snd_adsp_client *adsp_client, struct spacemit_snd_adsp_stream *s);
int spacemit_snd_adsp_stream_resume(struct spacemit_snd_adsp_client *adsp_client, struct spacemit_snd_adsp_stream *s);
int spacemit_snd_adsp_stream_pause(struct spacemit_snd_adsp_client *adsp_client, struct spacemit_snd_adsp_stream *s);
int spacemit_snd_adsp_stream_eos(struct spacemit_snd_adsp_client *adsp_client, msg_cbf_t callback, struct spacemit_snd_adsp_stream *s);
int spacemit_snd_adsp_stream_write_data(struct spacemit_snd_adsp_client *adsp_client, msg_cbf_t callback, \
	struct spacemit_snd_adsp_stream *s, struct audio_buffer *abuf);
int spacemit_snd_adsp_stream_read_data(struct spacemit_snd_adsp_client *adsp_client, msg_cbf_t callback, \
	struct spacemit_snd_adsp_stream *s, struct audio_buffer *abuf);
int spacemit_snd_adsp_stream_flush(struct spacemit_snd_adsp_client *adsp_client, struct spacemit_snd_adsp_stream *s);
int spacemit_snd_adsp_stream_set_volume(struct spacemit_snd_adsp_client *adsp_client, struct spacemit_snd_adsp_stream *s, uint32_t lvol, uint32_t rvol, uint32_t curve_duration);
int spacemit_snd_adsp_stream_set_mic_mute(struct spacemit_snd_adsp_client *adsp_client,
	struct spacemit_snd_adsp_stream *s, uint32_t mute, uint32_t curve_duration);
int spacemit_snd_adsp_stream_set_hw_dev(struct spacemit_snd_adsp_client *adsp_client,
	struct spacemit_snd_adsp_stream *s, uint32_t device);

int spacemit_snd_adsp_stream_set_route(struct spacemit_snd_adsp_client *adsp_client,
	uint32_t id, uint32_t dir, uint32_t backend_id);
int spacemit_snd_adsp_stream_set_rec_mode(struct spacemit_snd_adsp_client *adsp_client,
	struct spacemit_snd_adsp_stream *s, uint32_t mode);
struct spacemit_snd_adsp_client *spacemit_snd_get_adsp_client(int id, char *name);
void spacemit_snd_adsp_init(const char *name);

int spacemit_snd_adsp_register_event_handler(struct spacemit_snd_adsp_event_handler *handler);
void spacemit_snd_adsp_unregister_event_handler(struct spacemit_snd_adsp_event_handler *handler);

int spacemit_snd_adsp_codec_set_device(uint32_t dev_id, uint32_t enable);
int spacemit_snd_adsp_set_reg(uint32_t reg_adr, uint32_t mask, uint32_t val);
int spacemit_snd_adsp_get_reg(uint32_t reg_adr, uint32_t mask, uint32_t *val);

#endif

