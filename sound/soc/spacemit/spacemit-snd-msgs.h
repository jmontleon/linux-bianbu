// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2022 SPACEMIT Micro Limited
 */

#ifndef _SPACEMIT_SND_MSG_H
#define _SPACEMIT_SND_MSG_H

#include <linux/types.h>

#define IPC_MSG_DIR_SHIFT                    28
#define IPC_MSG_TYPE_SHFIT                   24
#define IPC_MSG_ID_SHIFT                     16
#define IPC_MSG_SEQ_NO_SHIFT                 0

#define IPC_MSG_DIR_MASK                     (0xF << IPC_MSG_DIR_SHIFT)
#define IPC_MSG_DIR_AP2ADSP                  (0 << IPC_MSG_DIR_SHIFT)
#define IPC_MSG_DIR_AP2ADSP_NEEDS_ACK        (1 << IPC_MSG_DIR_SHIFT)
#define IPC_MSG_DIR_ADSP2AP_REPLY            (2 << IPC_MSG_DIR_SHIFT)
#define IPC_MSG_DIR_ADSP2AP_NOTIFICATION     (3 << IPC_MSG_DIR_SHIFT)

#define IPC_MSG_NEEDS_ACK(x)                 (IPC_MSG_DIR_AP2ADSP_NEEDS_ACK == ((x) & IPC_MSG_DIR_MASK))

#define IPC_MSG_TYPE_GLOBAL                  (0 << IPC_MSG_TYPE_SHFIT)
#define IPC_MSG_TYPE_STREAM                  (1 << IPC_MSG_TYPE_SHFIT)

#define STREAM_ALLOC                         (0 << IPC_MSG_ID_SHIFT)
#define STREAM_FREE                          (1 << IPC_MSG_ID_SHIFT)
#define STREAM_RESUME                        (2 << IPC_MSG_ID_SHIFT)
#define STREAM_PAUSE                         (3 << IPC_MSG_ID_SHIFT)
#define STREAM_WRITE_DATA                    (4 << IPC_MSG_ID_SHIFT)
#define STREAM_READ_DATA                     (5 << IPC_MSG_ID_SHIFT)
#define STREAM_GET_RENDERED_FRAMES           (6 << IPC_MSG_ID_SHIFT)
#define STREAM_EOS                           (7 << IPC_MSG_ID_SHIFT)
#define STREAM_FLUSH                         (8 << IPC_MSG_ID_SHIFT)
#define STREAM_SET_VOLUME                    (9 << IPC_MSG_ID_SHIFT)
#define STREAM_SET_ROUTE                     (10 << IPC_MSG_ID_SHIFT)
#define STREAM_SET_MIC_MUTE                  (11 << IPC_MSG_ID_SHIFT)
#define STREAM_SET_RECORD_MODE               (12 << IPC_MSG_ID_SHIFT)
#define STREAM_SET_HW_DEV                    (13 << IPC_MSG_ID_SHIFT)


#define STREAM_EFFECT_GET_ENABLE             (16 << IPC_MSG_ID_SHIFT)
#define STREAM_EFFECT_SET_ENABLE             (17 << IPC_MSG_ID_SHIFT)
#define STREAM_EFFECT_GET_PARAMS             (18 << IPC_MSG_ID_SHIFT)
#define STREAM_EFFECT_SET_PARAMS             (19 << IPC_MSG_ID_SHIFT)
#define STREAM_VOICE_ENHANCEMENT_SET         (20 << IPC_MSG_ID_SHIFT)
#define STREAM_VOICE_ENHANCEMENT_GET         (21 << IPC_MSG_ID_SHIFT)

#define AUDIO_CODEC_SET_DEVICE               (22 << IPC_MSG_ID_SHIFT)
#define AUDIO_SET_REG                        (23 << IPC_MSG_ID_SHIFT)
#define AUDIO_GET_REG                        (24 << IPC_MSG_ID_SHIFT)

#define IPC_STREAM_MSG_ALLOC                 (IPC_MSG_DIR_AP2ADSP_NEEDS_ACK | IPC_MSG_TYPE_STREAM | STREAM_ALLOC)
#define IPC_STREAM_MSG_FREE                  (IPC_MSG_DIR_AP2ADSP_NEEDS_ACK | IPC_MSG_TYPE_STREAM | STREAM_FREE)
#define IPC_STREAM_MSG_RESUME                (IPC_MSG_DIR_AP2ADSP | IPC_MSG_TYPE_STREAM | STREAM_RESUME)
#define IPC_STREAM_MSG_PAUSE                 (IPC_MSG_DIR_AP2ADSP | IPC_MSG_TYPE_STREAM | STREAM_PAUSE)
#define IPC_STREAM_MSG_WRITE_DATA            (IPC_MSG_DIR_AP2ADSP_NEEDS_ACK | IPC_MSG_TYPE_STREAM | STREAM_WRITE_DATA)
#define IPC_STREAM_MSG_READ_DATA             (IPC_MSG_DIR_AP2ADSP_NEEDS_ACK | IPC_MSG_TYPE_STREAM | STREAM_READ_DATA)
#define IPC_STREAM_MSG_GET_RENDERED_FRAMES   (IPC_MSG_DIR_AP2ADSP_NEEDS_ACK | IPC_MSG_TYPE_STREAM | STREAM_GET_RENDERED_FRAMES)
#define IPC_STREAM_MSG_EOS                   (IPC_MSG_DIR_AP2ADSP | IPC_MSG_TYPE_STREAM | STREAM_EOS)
#define IPC_STREAM_MSG_FLUSH                 (IPC_MSG_DIR_AP2ADSP_NEEDS_ACK | IPC_MSG_TYPE_STREAM | STREAM_FLUSH)
#define IPC_STREAM_MSG_SET_VOLUME            (IPC_MSG_DIR_AP2ADSP | IPC_MSG_TYPE_STREAM | STREAM_SET_VOLUME)
#define IPC_STREAM_MSG_SET_ROUTE             (IPC_MSG_DIR_AP2ADSP | IPC_MSG_TYPE_STREAM | STREAM_SET_ROUTE)
#define IPC_STREAM_MSG_SET_MIC_MUTE          (IPC_MSG_DIR_AP2ADSP | IPC_MSG_TYPE_STREAM | STREAM_SET_MIC_MUTE)
#define IPC_STREAM_MSG_SET_HW_DEV            (IPC_MSG_DIR_AP2ADSP | IPC_MSG_TYPE_STREAM | STREAM_SET_HW_DEV)
#define IPC_STREAM_MSG_SET_RECORD_MODE       (IPC_MSG_DIR_AP2ADSP | IPC_MSG_TYPE_STREAM | STREAM_SET_RECORD_MODE)

#define IPC_GLOBAL_MSG_CODEC_SET_DEVICE      (IPC_MSG_DIR_AP2ADSP_NEEDS_ACK | IPC_MSG_TYPE_GLOBAL | AUDIO_CODEC_SET_DEVICE)

#define IPC_GLOBAL_MSG_AUDIO_SET_REG         (IPC_MSG_DIR_AP2ADSP_NEEDS_ACK | IPC_MSG_TYPE_GLOBAL | AUDIO_SET_REG)
#define IPC_GLOBAL_MSG_AUDIO_GET_REG         (IPC_MSG_DIR_AP2ADSP | IPC_MSG_TYPE_GLOBAL | AUDIO_GET_REG)

#define IPC_STREAM_MSG_EFFECT_GET_ENABLE     (IPC_MSG_DIR_AP2ADSP | IPC_MSG_TYPE_STREAM | STREAM_EFFECT_GET_ENABLE)
#define IPC_STREAM_MSG_EFFECT_SET_ENABLE     (IPC_MSG_DIR_AP2ADSP_NEEDS_ACK | IPC_MSG_TYPE_STREAM | STREAM_EFFECT_SET_ENABLE)
#define IPC_STREAM_MSG_EFFECT_GET_PARAMS     (IPC_MSG_DIR_AP2ADSP | IPC_MSG_TYPE_STREAM | STREAM_EFFECT_GET_PARAMS)
#define IPC_STREAM_MSG_EFFECT_SET_PARAMS     (IPC_MSG_DIR_AP2ADSP_NEEDS_ACK | IPC_MSG_TYPE_STREAM | STREAM_EFFECT_SET_PARAMS)

#define IPC_STREAM_MSG_VOICE_ENHANCEMENT_SET (IPC_MSG_DIR_AP2ADSP_NEEDS_ACK | IPC_MSG_TYPE_STREAM | STREAM_VOICE_ENHANCEMENT_SET)
#define IPC_STREAM_MSG_VOICE_ENHANCEMENT_GET (IPC_MSG_DIR_AP2ADSP_NEEDS_ACK | IPC_MSG_TYPE_STREAM | STREAM_VOICE_ENHANCEMENT_GET)

#define IPC_MSG_SEQ_NO_MASK                  0xFFFF
#define IPC_MSG_SEQ_NO(x)                    ((x) & IPC_MSG_SEQ_NO_MASK)

/* match with adsp definition in include/audio/audio_ipc.h */
#define IPC_MSG_NOTIFY_TYPE_JACK_EVENT       0x3

#define IPC_GET_NOTIFY_TYPE(x)               (((x) & (0xF << IPC_MSG_TYPE_SHFIT)) >> IPC_MSG_TYPE_SHFIT)

/*
 * maximum bytes of all types of messages
 */
#define IPC_MSG_BYTES_MAX                    256

enum {
	STREAM_DIR_PLAYBACK = 0x1,
	STREAM_DIR_CAPTURE  = 0x2
};

#define STREAM_TYPE(dir, id) (((dir) << 16) | (id))

enum {
	/*
	 * follow definitions in compress_params.h
	 */
	STREAM_FORMAT_PCM = 0x1,
	STREAM_FORMAT_MP3 = 0x2,
	STREAM_FORMAT_AAC = 0x6
};

enum {
	EFFECT_VIRTUALIZER,
	EFFECT_REVERB,
	EFFECT_BASS_BOOST,
	EFFECT_EQ,
};

struct spacemit_snd_ipc_stream_alloc {
	uint32_t stream_type;
	uint32_t format;
	uint32_t bits;
	uint32_t sample_rate;
	uint32_t channels;
	uint32_t bit_rate;
	uint32_t hw_dev;
	uint32_t dma_enhancement;
};

struct spacemit_snd_ipc_stream_free {
	uint32_t stream_type;
};

struct spacemit_snd_ipc_stream_resume {
	uint32_t stream_type;
};

struct spacemit_snd_ipc_stream_pause {
	uint32_t stream_type;
};

struct spacemit_snd_ipc_stream_eos {
	uint32_t stream_type;
};

struct spacemit_snd_ipc_stream_write_data {
	uint32_t stream_type;
	uint32_t paddr;
	uint32_t filled;
};

struct spacemit_snd_ipc_stream_read_data {
	uint32_t stream_type;
	uint32_t paddr;
	uint32_t required;
};

struct spacemit_snd_ipc_stream_write_data_done {
	uint32_t stream_type;
};

struct spacemit_snd_ipc_stream_flush {
	uint32_t stream_type;
};

struct spacemit_snd_ipc_stream_get_rendered_frames {
	uint32_t stream_type;
};

struct spacemit_snd_ipc_stream_get_rendered_frames_reply {
	uint32_t rendered_frames;
};

struct spacemit_snd_ipc_stream_set_volume {
	uint32_t stream_type;
	uint32_t left_volume;
	uint32_t right_volume;
	uint32_t curve_duration;
};

struct spacemit_snd_ipc_stream_set_mic_mute {
	uint32_t stream_type;
	uint32_t mute;
	uint32_t curve_duration;
};

struct spacemit_snd_ipc_stream_set_hw_dev {
	uint32_t stream_type;
	uint32_t device;
};

struct spacemit_snd_ipc_stream_set_route {
	uint32_t stream_type;/*include stream id and dir */
	uint32_t backend_id;
};

struct spacemit_snd_ipc_codec_set_device {
	uint32_t dev_id;
	uint32_t enable;
};

struct spacemit_snd_ipc_audio_reg {
	uint32_t reg_adr;
	uint32_t mask;
	uint32_t val;
};

struct spacemit_snd_ipc_stream_set_rec_mode {
	uint32_t stream_type;/*include stream id and dir */
	uint32_t mode;
};

struct spacemit_snd_ipc_jack_event {
    uint32_t report_type;
    uint32_t jack_type;
    uint32_t key_type;
};

#define REPORT_JACK_TYPE 0x1
#define REPORT_KEY_TYPE  0x2

#define SPACEMIT_EFFECT_PARAMS 0
#define SPACEMIT_EFFECT_ENABLE 1
#define SPACEMIT_VOICE_PARAMS_LOAD 2


enum {
    VE_MODULE_HPF = 0,
    VE_MODULE_LPF,
    VE_MODULE_EQ,
    VE_MODULE_EC,
    VE_MODULE_NS,
    VE_MODULE_AVC,
    VE_MODULE_VOL,

    VE_MODULE_MAX
};

enum {
    VE_PATH_TX = 0,
    VE_PATH_RX
};

#endif

