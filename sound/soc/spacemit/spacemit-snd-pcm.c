// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2022 SPACEMIT Micro Limited
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/of.h>

#include <sound/pxa2xx-lib.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/dmaengine_pcm.h>

#include "spacemit-snd.h"
#include "spacemit-snd-adsp.h"
#include "spacemit-snd-msgs.h"
#include "spacemit-snd-memory.h"

struct spacemit_snd_pcm_stream {
	size_t period_bytes;
	size_t buffer_bytes;
	size_t offset_bytes; // 0 ~ buffer_bytes
	int period_msecs;

	unsigned long prepared;
	atomic_t out_bufs;

	wait_queue_head_t eos_wait;

	struct audio_buffer *cur_buf;

	struct snd_pcm_substream *substream;
	struct spacemit_snd_adsp_stream  adsp_stream;
	struct spacemit_snd_adsp_client *adsp_client;
	struct audio_memory_manager *memmgr;
};

#define SPACEMIT_SND_PCM_PLAYBACK_MIN_CHANNELS      1
#define SPACEMIT_SND_PCM_PLAYBACK_MAX_CHANNELS      8

#define SPACEMIT_SND_PCM_PLAYBACK_MIN_NUM_PERIODS   2
#define SPACEMIT_SND_PCM_PLAYBACK_MAX_NUM_PERIODS   8
#define SPACEMIT_SND_PCM_PLAYBACK_MIN_PERIOD_SIZE   128
#define SPACEMIT_SND_PCM_PLAYBACK_MAX_PERIOD_SIZE   12288

#define SPACEMIT_SND_PCM_CAPTURE_MIN_CHANNELS       1
#define SPACEMIT_SND_PCM_CAPTURE_MAX_CHANNELS       4

#define SPACEMIT_SND_PCM_CAPTURE_MIN_NUM_PERIODS    2
#define SPACEMIT_SND_PCM_CAPTURE_MAX_NUM_PERIODS    8
#define SPACEMIT_SND_PCM_CAPTURE_MIN_PERIOD_SIZE    64
#define SPACEMIT_SND_PCM_CAPTURE_MAX_PERIOD_SIZE    16384

static unsigned int supported_sample_rates[] = {
	8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000,
	88200, 96000, 176400, 192000
};

static struct snd_pcm_hw_constraint_list constraints_sample_rates = {
	.count = ARRAY_SIZE(supported_sample_rates),
	.list = supported_sample_rates,
	.mask = 0,
};

static struct snd_pcm_hardware spacemit_snd_pcm_hardware_playback = {
	.info =                 (SNDRV_PCM_INFO_MMAP |
				SNDRV_PCM_INFO_BLOCK_TRANSFER |
				SNDRV_PCM_INFO_MMAP_VALID |
				SNDRV_PCM_INFO_INTERLEAVED |
				SNDRV_PCM_INFO_PAUSE | SNDRV_PCM_INFO_RESUME),
	.formats =              SNDRV_PCM_FMTBIT_S16_LE,
	.rates =                SNDRV_PCM_RATE_48000,
	.rate_min =             SNDRV_PCM_RATE_48000,
	.rate_max =             SNDRV_PCM_RATE_48000,
	.channels_min =         SPACEMIT_SND_PCM_PLAYBACK_MIN_CHANNELS,
	.channels_max =         SPACEMIT_SND_PCM_PLAYBACK_MAX_CHANNELS,
	.buffer_bytes_max =     SPACEMIT_SND_PCM_PLAYBACK_MAX_NUM_PERIODS *
				SPACEMIT_SND_PCM_PLAYBACK_MAX_PERIOD_SIZE,
	.period_bytes_min =	SPACEMIT_SND_PCM_PLAYBACK_MIN_PERIOD_SIZE,
	.period_bytes_max =     SPACEMIT_SND_PCM_PLAYBACK_MAX_PERIOD_SIZE,
	.periods_min =          SPACEMIT_SND_PCM_PLAYBACK_MIN_NUM_PERIODS,
	.periods_max =          SPACEMIT_SND_PCM_PLAYBACK_MAX_NUM_PERIODS,
	.fifo_size =            0,
};

static struct snd_pcm_hardware spacemit_snd_pcm_hardware_capture = {
	.info =                 (SNDRV_PCM_INFO_MMAP |
				SNDRV_PCM_INFO_BLOCK_TRANSFER |
				SNDRV_PCM_INFO_MMAP_VALID |
				SNDRV_PCM_INFO_INTERLEAVED |
				SNDRV_PCM_INFO_PAUSE | SNDRV_PCM_INFO_RESUME),
	.formats =              (SNDRV_PCM_FMTBIT_S16_LE |
				SNDRV_PCM_FMTBIT_S24_LE),
	.rates =                SNDRV_PCM_RATE_8000_48000,
	.rate_min =             8000,
	.rate_max =             48000,
	.channels_min =         SPACEMIT_SND_PCM_CAPTURE_MIN_CHANNELS,
	.channels_max =         SPACEMIT_SND_PCM_CAPTURE_MAX_CHANNELS,
	.buffer_bytes_max =     SPACEMIT_SND_PCM_CAPTURE_MAX_NUM_PERIODS *
				SPACEMIT_SND_PCM_CAPTURE_MAX_PERIOD_SIZE,
	.period_bytes_min =	SPACEMIT_SND_PCM_CAPTURE_MIN_PERIOD_SIZE,
	.period_bytes_max =     SPACEMIT_SND_PCM_CAPTURE_MAX_PERIOD_SIZE,
	.periods_min =          SPACEMIT_SND_PCM_CAPTURE_MIN_NUM_PERIODS,
	.periods_max =          SPACEMIT_SND_PCM_CAPTURE_MAX_NUM_PERIODS,
	.fifo_size =            0,
};

static int spacemit_snd_pcm_open(struct snd_soc_component *component, struct snd_pcm_substream *substream)
{
	int ret;
	struct snd_pcm_runtime *runtime;
	struct spacemit_snd_pcm_stream *pcm_stream;

	pr_debug("enter %s\n", __func__);

	pcm_stream = kzalloc(sizeof(struct spacemit_snd_pcm_stream), GFP_KERNEL);
	if (!pcm_stream)
		return -ENOMEM;

	pr_debug("pcm_stream 0x%p\n", pcm_stream);
	pcm_stream->substream = substream;

	init_waitqueue_head(&pcm_stream->eos_wait);
	atomic_set(&pcm_stream->out_bufs, 0);

	runtime = substream->runtime;
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		runtime->hw = spacemit_snd_pcm_hardware_playback;
	else if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
		runtime->hw = spacemit_snd_pcm_hardware_capture;
	else {
		pr_err("invalid stream type %d\n", substream->stream);
		kfree(pcm_stream);
		return -EINVAL;
	}

	ret = snd_pcm_hw_constraint_list(runtime, 0,
				SNDRV_PCM_HW_PARAM_RATE, &constraints_sample_rates);
	if (ret < 0)
		pr_debug("snd_pcm_hw_constraint_list failed\n");

	ret = snd_pcm_hw_constraint_integer(runtime, SNDRV_PCM_HW_PARAM_PERIODS);
	if (ret < 0)
		pr_debug("snd_pcm_hw_constraint_integer failed\n");

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		ret = snd_pcm_hw_constraint_minmax(runtime, SNDRV_PCM_HW_PARAM_BUFFER_BYTES,
			SPACEMIT_SND_PCM_PLAYBACK_MIN_NUM_PERIODS * SPACEMIT_SND_PCM_PLAYBACK_MIN_PERIOD_SIZE,
			SPACEMIT_SND_PCM_PLAYBACK_MAX_NUM_PERIODS * SPACEMIT_SND_PCM_PLAYBACK_MAX_PERIOD_SIZE);
		if (ret < 0)
			pr_err("constraint for buffer bytes min max ret = %d\n", ret);
	} else if (substream->stream == SNDRV_PCM_STREAM_CAPTURE) {
		ret = snd_pcm_hw_constraint_minmax(runtime,
			SNDRV_PCM_HW_PARAM_BUFFER_BYTES,
			SPACEMIT_SND_PCM_CAPTURE_MIN_NUM_PERIODS * SPACEMIT_SND_PCM_CAPTURE_MIN_PERIOD_SIZE,
			SPACEMIT_SND_PCM_CAPTURE_MAX_NUM_PERIODS * SPACEMIT_SND_PCM_CAPTURE_MAX_PERIOD_SIZE);
		if (ret < 0)
			pr_err("constraint for buffer bytes min max ret = %d\n", ret);
	}

	ret = snd_pcm_hw_constraint_step(runtime, 0,
		SNDRV_PCM_HW_PARAM_PERIOD_BYTES, 32);
	if (ret < 0)
		pr_err("constraint for period bytes step ret = %d\n", ret);

	ret = snd_pcm_hw_constraint_step(runtime, 0,
		SNDRV_PCM_HW_PARAM_BUFFER_BYTES, 32);
	if (ret < 0)
		pr_err("constraint for buffer bytes step ret = %d\n", ret);

	runtime->private_data = pcm_stream;

	return 0;
}

static int spacemit_snd_pcm_close(struct snd_soc_component *component, struct snd_pcm_substream *substream)
{
#define EOS_MIN_TIMEOUT_LENGTH  100
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct spacemit_snd_pcm_stream *pcm_stream = runtime->private_data;

	pr_debug("enter %s\n", __func__);

	if (pcm_stream) {
		int ret;
		uint32_t timeout;

		if (pcm_stream->adsp_client) {
			ret = spacemit_snd_adsp_stream_eos(pcm_stream->adsp_client, NULL, &pcm_stream->adsp_stream);
			if (ret)
				pr_err("fatal error, failed to issue eos\n");
		}

		if (runtime->frame_bits == 0 || runtime->rate == 0) {
			timeout = EOS_MIN_TIMEOUT_LENGTH;
		} else {
#define MULTIPLIER 50 // for safety guard
			timeout = runtime->period_size * HZ * MULTIPLIER /
				((runtime->frame_bits / 8) * runtime->rate);
			if (timeout < EOS_MIN_TIMEOUT_LENGTH)
				timeout = EOS_MIN_TIMEOUT_LENGTH;
		}

		pr_debug("wait for eos done, timeout: %d\n", timeout);
		ret = wait_event_timeout(pcm_stream->eos_wait,
			0 == atomic_read(&pcm_stream->out_bufs), timeout);
		if (!ret)
			pr_err("%s: warning, eos not finished, timeout=%d ms\n", __func__, timeout);

		if (pcm_stream->adsp_client) {
			ret = spacemit_snd_adsp_stream_pause(pcm_stream->adsp_client, &pcm_stream->adsp_stream);
			if (ret < 0)
				pr_err("failed to pause stream\n");

			ret = spacemit_snd_adsp_stream_free(pcm_stream->adsp_client, &pcm_stream->adsp_stream);
			if (ret < 0)
				pr_err("failed to free stream\n");

			pcm_stream->adsp_client = NULL;
		}

		if (pcm_stream->memmgr) {
			spacemit_snd_free_memory(pcm_stream->memmgr);
			pcm_stream->memmgr = NULL;
		}

		kfree(pcm_stream);
	}

	runtime->private_data = NULL;

	return 0;
}

static int spacemit_snd_pcm_hw_params(struct snd_soc_component *component,
                                 struct snd_pcm_substream *substream,
                                 struct snd_pcm_hw_params *params)
{
	int ret;
	int dir;
	struct snd_pcm_runtime *runtime;
	struct snd_dma_buffer *dma_buf;
	struct spacemit_snd_pcm_stream *pcm_stream;

	pr_debug("enter %s, hw_params add:%p\n", __func__, params);

	runtime = substream->runtime;
	dma_buf = &substream->dma_buffer;

	pcm_stream = runtime->private_data;
	dir = substream->stream == SNDRV_PCM_STREAM_PLAYBACK ? MEMORY_DIR_OUT : MEMORY_DIR_IN;

	if (pcm_stream->memmgr) {
		pr_info("%s, dir=%d, pcm_stream->memmgr=0x%px\n", __func__, dir, pcm_stream->memmgr);
	}

	pr_debug("buffer bytes: %d, periods: %d\n", \
		params_buffer_bytes(params), params_periods(params));
	ret = spacemit_snd_alloc_memory(&pcm_stream->memmgr, dir,
			params_periods(params), (params_buffer_bytes(params) / params_periods(params)));
	if (ret < 0) {
		pr_err("failed to allocate memory for substream %s\n", substream->name);
		return -ENOMEM;
	}

	dma_buf->dev.type = SNDRV_DMA_TYPE_DEV;
	dma_buf->dev.dev = substream->pcm->card->dev;
	dma_buf->private_data = NULL;
	dma_buf->area = spacemit_snd_get_memory_vaddr(pcm_stream->memmgr);
	ret =  spacemit_snd_get_memory_paddr(pcm_stream->memmgr, &dma_buf->addr);
	if (!dma_buf->area || ret < 0) {
		spacemit_snd_free_memory(pcm_stream->memmgr);
		pcm_stream->memmgr = NULL;
		pr_err("invalid memory for substream %s\n", substream->name);
		return -ENOMEM;
	}
	dma_buf->bytes = params_buffer_bytes(params);

	snd_pcm_set_runtime_buffer(substream, &substream->dma_buffer);

	return 0;
}

static void __pcm_capture_read_data_done(void *arg, void *data)
{
	struct audio_buffer *abuf;
	struct spacemit_snd_adsp_stream *adsp_stream;
	struct spacemit_snd_pcm_stream *pcm_stream;

	//pr_debug("enter %s\n", __func__);
	adsp_stream = (struct spacemit_snd_adsp_stream *)arg;
	if (!adsp_stream) {
		pr_err("%s:adsp_stream null,FAIL\n", __func__);
		return;
	}

	pcm_stream = container_of(adsp_stream, struct spacemit_snd_pcm_stream, adsp_stream);

	abuf = spacemit_snd_get_first_rendered_buffer(pcm_stream->memmgr);
	BUG_ON(!abuf);

	pcm_stream->offset_bytes += pcm_stream->period_bytes;
	if (pcm_stream->offset_bytes >= pcm_stream->buffer_bytes)
		pcm_stream->offset_bytes = 0;

	spacemit_snd_finish_one_buffer(pcm_stream->memmgr, abuf);
	snd_pcm_period_elapsed(pcm_stream->substream);

	return;
}

static int spacemit_snd_pcm_prepare(struct snd_soc_component *component, struct snd_pcm_substream *substream)
{
	int ret;

	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_pcm_runtime *soc_runtime = (struct snd_soc_pcm_runtime *)substream->private_data;
	struct snd_soc_dai *cpu_dai = asoc_rtd_to_cpu(soc_runtime, 0);
	struct spacemit_snd_pcm_stream *pcm_stream = runtime->private_data;
	int stream_id = cpu_dai->id;

	pcm_stream->adsp_client = spacemit_snd_get_adsp_client(0, substream->pcm->card->shortname);
	if (!pcm_stream->adsp_client) {
		pr_err("failed to get adsp client\n");
		return -ENXIO;
	}

	if (pcm_stream->prepared)
		return 0;

	pcm_stream->period_bytes = snd_pcm_lib_period_bytes(substream);
	pcm_stream->buffer_bytes = snd_pcm_lib_buffer_bytes(substream);

	pr_debug("%s, period_bytes: %zd, buffer_bytes: %zd\n", __func__, \
		pcm_stream->period_bytes, pcm_stream->buffer_bytes);

	pcm_stream->offset_bytes = 0;

	pcm_stream->adsp_stream.type = STREAM_TYPE(substream->stream == SNDRV_PCM_STREAM_PLAYBACK ?
		STREAM_DIR_PLAYBACK : STREAM_DIR_CAPTURE, stream_id);
	pcm_stream->adsp_stream.format = STREAM_FORMAT_PCM;

	pcm_stream->adsp_stream.bits = 16;
	pcm_stream->adsp_stream.channels = runtime->channels;
	pcm_stream->adsp_stream.sample_rate = runtime->rate;

	pcm_stream->period_msecs = bytes_to_frames(runtime, pcm_stream->period_bytes) * 1000 /
					pcm_stream->adsp_stream.sample_rate;

	ret = spacemit_snd_adsp_stream_alloc(pcm_stream->adsp_client, &pcm_stream->adsp_stream);
	if (ret) {
		pr_err("failed to get adsp stream\n");
		goto close_client;
	}

	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE) {
		struct audio_buffer *abuf;
		struct spacemit_snd_adsp_client *adsp_client = pcm_stream->adsp_client;
		struct audio_memory_manager *memmgr = pcm_stream->memmgr;
		while ((abuf = spacemit_snd_get_free_buffer(memmgr))) {
			if (spacemit_snd_adsp_stream_read_data(adsp_client, __pcm_capture_read_data_done, \
					&pcm_stream->adsp_stream, abuf) < 0)
				goto free_stream;
		}
	}
	pcm_stream->prepared = 1;

	return 0;

free_stream:
	spacemit_snd_adsp_stream_free(pcm_stream->adsp_client, &pcm_stream->adsp_stream);
close_client:
	pcm_stream->adsp_client = NULL;

	return -EIO;
}

static int spacemit_snd_pcm_trigger(struct snd_soc_component *component, struct snd_pcm_substream *substream, int cmd)
{
	int ret = 0;
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct spacemit_snd_pcm_stream *pcm_stream = runtime->private_data;
	struct spacemit_snd_adsp_client *adsp_client = pcm_stream->adsp_client;

	pr_debug("enter %s, cmd: %d\n", __func__, cmd);

	if (!adsp_client) {
		pr_err("adsp is not conncted..., quit\n");
		return -ENODEV;
	}

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		pr_debug("SNDRV_PCM_TRIGGER_START/resume/release:cmd=%d\n", cmd);
		ret = spacemit_snd_adsp_stream_resume(adsp_client, &pcm_stream->adsp_stream);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		pr_debug("SNDRV_PCM_TRIGGER_STOP\n");
		if (substream->stream != SNDRV_PCM_STREAM_PLAYBACK) {
			ret = spacemit_snd_adsp_stream_pause(adsp_client, &pcm_stream->adsp_stream);
			break;
		}

		ret = spacemit_snd_adsp_stream_eos(adsp_client, NULL, &pcm_stream->adsp_stream);
		break;
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		pr_debug("SNDRV_PCM_TRIGGER_PAUSE,cmd %d\n", cmd);
		ret = spacemit_snd_adsp_stream_pause(adsp_client, &pcm_stream->adsp_stream);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static snd_pcm_uframes_t spacemit_snd_pcm_pointer(struct snd_soc_component *component, struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct spacemit_snd_pcm_stream *pcm_stream = runtime->private_data;
	return bytes_to_frames(runtime, pcm_stream->offset_bytes);
}

static int spacemit_snd_pcm_mmap(struct snd_soc_component *component, struct snd_pcm_substream *substream,
			struct vm_area_struct *vma)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	unsigned long off = vma->vm_pgoff;

	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
	return remap_pfn_range(vma, vma->vm_start,
			__phys_to_pfn(runtime->dma_addr) + off,
				vma->vm_end - vma->vm_start, vma->vm_page_prot);
}

static void __pcm_playback_write_data_done(void *arg, void *data)
{
	struct audio_buffer *abuf;
	struct spacemit_snd_adsp_stream *adsp_stream;
	struct spacemit_snd_pcm_stream *pcm_stream;
	struct spacemit_snd_adsp_client *adsp_client;

	adsp_stream = (struct spacemit_snd_adsp_stream *)arg;
	if (!adsp_stream) {
		pr_err("write data done, stream data corrupted\n");
		return;
	}

	pcm_stream = container_of(adsp_stream, struct spacemit_snd_pcm_stream, adsp_stream);

	adsp_client = pcm_stream->adsp_client;
	if (!adsp_client) {
		pr_err("write data done, client data corrupted\n");
		return;
	}

	atomic_dec(&pcm_stream->out_bufs);
	wake_up(&pcm_stream->eos_wait); //This only takes effect when end of stream

	abuf = spacemit_snd_get_first_rendered_buffer(pcm_stream->memmgr);
	BUG_ON(!abuf);

	pcm_stream->offset_bytes += abuf->filled;
	if (pcm_stream->offset_bytes >= pcm_stream->buffer_bytes)
		pcm_stream->offset_bytes = 0;

	spacemit_snd_finish_one_buffer(pcm_stream->memmgr, abuf);
	snd_pcm_period_elapsed(pcm_stream->substream);
	pr_debug("enter %s, pcm_stream->buffer_bytes=%zd\n", __func__, pcm_stream->buffer_bytes);

	return;
}

static int _spacemit_snd_pcm_playback_copy(struct spacemit_snd_pcm_stream *pcm_stream, int channel,
                                      snd_pcm_uframes_t pos, void __user *buf, unsigned long bytes)
{
	struct audio_buffer *abuf;
	ssize_t offset, copy_bytes, buf_avail_bytes;

	ssize_t buf_bytes = bytes;
	struct spacemit_snd_adsp_client *adsp_client = pcm_stream->adsp_client;

	//pr_debug("enter %s, buf_bytes=%zd\n", __func__, buf_bytes);

	offset = 0;
	while (buf_bytes) {
		abuf = spacemit_snd_get_next_buffer(pcm_stream->memmgr, 2*pcm_stream->period_msecs); // multiple by 2 for safety guard
		if (!abuf) {
			pr_err("failed to get next available bufer\n");
			return -ENOMEM;
		}

		copy_bytes = buf_bytes;
		buf_avail_bytes = abuf->size - abuf->filled;
		if (copy_bytes > buf_avail_bytes)
			copy_bytes = buf_avail_bytes;

		if (copy_from_user(abuf->vaddr, buf + offset, copy_bytes)) {
			pr_err("failed to copy from user\n");
			return -EFAULT;
		}

		abuf->filled = copy_bytes;

		offset += copy_bytes;
		buf_bytes -= copy_bytes;

		if (spacemit_snd_adsp_stream_write_data(adsp_client, __pcm_playback_write_data_done,\
				&pcm_stream->adsp_stream, abuf) < 0) {
			pr_err("failed to render buffers\n");
			return -EIO;
		}
		atomic_inc(&pcm_stream->out_bufs);
	}

	return 0;
}

static int _spacemit_snd_pcm_capture_copy(struct spacemit_snd_pcm_stream *pcm_stream, int channel,
                                     snd_pcm_uframes_t pos, void __user *buf, unsigned long bytes)
{
	struct audio_buffer *cur_buf;
	ssize_t copy_bytes, buf_avail_bytes;

	ssize_t buf_bytes = bytes;
	struct spacemit_snd_adsp_client *adsp_client = pcm_stream->adsp_client;

	//pr_debug("enter %s, buffer bytes: %zd\n", __func__, buf_bytes);

	while (buf_bytes) {
		if (!pcm_stream->cur_buf) {
			pcm_stream->cur_buf = spacemit_snd_get_next_buffer(pcm_stream->memmgr, 2*pcm_stream->period_msecs);
			if (!pcm_stream->cur_buf) {
				pr_err("%s:pcm_stream->cur_buf=NULL,FAIL\n", __func__);
				return -ENOMEM;
			}
		}

		cur_buf = pcm_stream->cur_buf;
		copy_bytes = buf_bytes;
		buf_avail_bytes = cur_buf->size - cur_buf->offset;
		if (copy_bytes > buf_avail_bytes)
			copy_bytes = buf_avail_bytes;

		if (copy_to_user(buf, cur_buf->vaddr + cur_buf->offset, copy_bytes)) {
			pr_err("%s: copy_to_user FAIL\n", __func__);
			return -EFAULT;
		}

		cur_buf->offset += copy_bytes;
		buf_bytes -= copy_bytes;
		if (cur_buf->offset == cur_buf->size) {
			if (spacemit_snd_adsp_stream_read_data(adsp_client, __pcm_capture_read_data_done,\
					&pcm_stream->adsp_stream, cur_buf) < 0) {
				pr_err("%s: failed\n", __func__);
				return -EIO;
			}
			pcm_stream->cur_buf = NULL;
		}
	}

	return 0;

}

static int spacemit_snd_pcm_copy(struct snd_soc_component *component,
                            struct snd_pcm_substream *substream, int channel,
                            snd_pcm_uframes_t pos, void __user  *buf, unsigned long bytes)
{
	int ret = 0;

	struct snd_pcm_runtime *runtime = substream->runtime;
	struct spacemit_snd_pcm_stream *pcm_stream = runtime->private_data;
	if (!pcm_stream->adsp_client) {
		return -ENODEV;
	}
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		ret = _spacemit_snd_pcm_playback_copy(pcm_stream, channel, pos, buf, bytes);
	else if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
		ret = _spacemit_snd_pcm_capture_copy(pcm_stream, channel, pos, buf, bytes);

	return ret;
}

static int spacemit_snd_pcm_new(struct snd_soc_component *component, struct snd_soc_pcm_runtime *rtd)
{
	pr_debug("enter %s\n", __func__);

	return 0;
}

static struct snd_soc_component_driver spacemit_snd_pcm_component = {
	.open = spacemit_snd_pcm_open,
	.close = spacemit_snd_pcm_close,
	.hw_params = spacemit_snd_pcm_hw_params,
	.prepare = spacemit_snd_pcm_prepare,
	.trigger = spacemit_snd_pcm_trigger,
	.pointer = spacemit_snd_pcm_pointer,
	.mmap = spacemit_snd_pcm_mmap,
	.copy_user = spacemit_snd_pcm_copy,
	.pcm_construct = spacemit_snd_pcm_new
};

static int spacemit_snd_pcm_pdev_probe(struct platform_device *pdev)
{
	pr_debug("%s: dev name %s\n", __func__, dev_name(&pdev->dev));
	return snd_soc_register_component(&pdev->dev, &spacemit_snd_pcm_component, NULL, 0);
}

static int spacemit_snd_pcm_pdev_remove(struct platform_device *pdev)
{
	snd_soc_unregister_component(&pdev->dev);
	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id spacemit_snd_pcm_dt_match[] = {
	{.compatible = "spacemit,spacemit-snd-pcm"},
	{}
};
#endif

static struct platform_driver spacemit_snd_pcm_pdrv = {
	.driver = {
		.name = "spacemit-snd-pcm",
		.of_match_table = of_match_ptr(spacemit_snd_pcm_dt_match),
	},
	.probe = spacemit_snd_pcm_pdev_probe,
	.remove = spacemit_snd_pcm_pdev_remove,
};

#if IS_MODULE(CONFIG_SND_SOC_SPACEMIT)
int spacemit_snd_register_pcm_pdrv(void)
{
	return platform_driver_register(&spacemit_snd_pcm_pdrv);
}
EXPORT_SYMBOL(spacemit_snd_register_pcm_pdrv);

void spacemit_snd_unregister_pcm_pdrv(void)
{
	platform_driver_unregister(&spacemit_snd_pcm_pdrv);
}
EXPORT_SYMBOL(spacemit_snd_unregister_pcm_pdrv);
#else
module_platform_driver(spacemit_snd_pcm_pdrv);
#endif

MODULE_DESCRIPTION("SPACEMIT Aquila ASoC PCM Platform Driver");
MODULE_LICENSE("GPL");

