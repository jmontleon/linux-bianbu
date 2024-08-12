// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2023 SPACEMIT
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/dmaengine_pcm.h>
#include <linux/genalloc.h>

#define DRV_NAME "spacemit-snd-dma"

#define I2S_PERIOD_SIZE          1024
#define I2S_PERIOD_COUNT         4
#define I2S0_REG_BASE            0xD4026000
#define I2S1_REG_BASE            0xD4026800
#define DATAR                    0x10    /* SSP Data Register */

#define DMA_I2S0 0
#define DMA_I2S1 1
#define DMA_HDMI 2

#define HDMI_REFORMAT_ENABLE
#define I2S_HDMI_REG_BASE        0xC0883900
#define HDMI_TXDATA              0x80
#define HDMI_PERIOD_SIZE         480

#define SAMPLE_PRESENT_FLAG_OFFSET      31
#define AUDIO_FRAME_START_BIT_OFFSET    30
#define PARITY_BIT_OFFSET               27
#define CHANNEL_STATUS_OFFSET           26
#define VALID_OFFSET                    24

#define CS_CTRL1 ((1 << SAMPLE_PRESENT_FLAG_OFFSET) | (1 << AUDIO_FRAME_START_BIT_OFFSET))
#define CS_CTRL2 ((1 << SAMPLE_PRESENT_FLAG_OFFSET) | (0 << AUDIO_FRAME_START_BIT_OFFSET))

#define CS_SAMPLING_FREQUENCY           25
#define CS_MAX_SAMPLE_WORD              32

#define P2(n) n, n^1, n^1, n
#define P4(n) P2(n), P2(n^1), P2(n^1), P2(n)
#define P6(n) P4(n), P4(n^1), P4(n^1), P4(n)

struct spacemit_snd_dmadata {
	struct dma_chan *dma_chan;
	unsigned int dma_id;
	dma_cookie_t cookie;
	spinlock_t dma_lock;
	void *private_data;

	int stream;
	/*DOMAIN:config from userspace*/
	struct snd_pcm_substream *substream;
	unsigned long pos;
	bool playback_data;
};

struct spacemit_snd_soc_device {
	struct spacemit_snd_dmadata dmadata[2];
	unsigned long pos;
	bool playback_en;
	bool capture_en;
	spinlock_t lock;
};

struct hdmi_priv {
	dma_addr_t phy_addr;
	void __iomem	*buf_base;
};

/* HDMI initalization data */
struct hdmi_codec_priv {
    uint32_t srate;
    uint32_t channels;
    uint8_t iec_offset;
};

static struct hdmi_codec_priv hdmi_ptr = {0};
static const bool ParityTable256[256] =
{
    P6(0), P6(1), P6(1), P6(0)
};
static struct hdmi_priv priv;
static dma_addr_t hdmiraw_dma_addr;
static dma_addr_t hdmipcm_dma_addr;
static unsigned char *hdmiraw_dma_area;	/* DMA area */
#ifdef HDMI_REFORMAT_ENABLE
static unsigned char *hdmiraw_dma_area_tmp;
#endif

static int spacemit_snd_dma_init(struct device *paraent, struct spacemit_snd_soc_device *dev,
			     struct spacemit_snd_dmadata *dmadata);

static const struct snd_pcm_hardware spacemit_snd_pcm_hardware = {
	.info		  = SNDRV_PCM_INFO_INTERLEAVED |
			    SNDRV_PCM_INFO_BATCH,
	.formats          = SNDRV_PCM_FMTBIT_S16_LE,
	.rates            = SNDRV_PCM_RATE_48000,
	.rate_min         = SNDRV_PCM_RATE_48000,
	.rate_max         = SNDRV_PCM_RATE_48000,
	.channels_min     = 2,
	.channels_max     = 2,
	.buffer_bytes_max = I2S_PERIOD_SIZE * I2S_PERIOD_COUNT * 4,
	.period_bytes_min = I2S_PERIOD_SIZE * 4,
	.period_bytes_max = I2S_PERIOD_SIZE * 4,
	.periods_min	  = I2S_PERIOD_COUNT,
	.periods_max	  = I2S_PERIOD_COUNT,
};

static const struct snd_pcm_hardware spacemit_snd_pcm_hardware_hdmi = {
	.info		  = SNDRV_PCM_INFO_INTERLEAVED |
			    SNDRV_PCM_INFO_BATCH,
	.formats	  = SNDRV_PCM_FMTBIT_S16_LE,
	.rates		  = SNDRV_PCM_RATE_48000,
	.rate_min	  = SNDRV_PCM_RATE_48000,
	.rate_max	  = SNDRV_PCM_RATE_48000,
	.channels_min	  = 2,
	.channels_max	  = 2,
	.buffer_bytes_max = HDMI_PERIOD_SIZE * 4 * 4,
	.period_bytes_min = HDMI_PERIOD_SIZE * 4,
	.period_bytes_max = HDMI_PERIOD_SIZE * 4,
	.periods_min	  = 4,
	.periods_max	  = 4,
};
static int spacemit_dma_slave_config(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params,
		struct dma_slave_config *slave_config,
		int dma_id)
{
	int ret = snd_hwparams_to_dma_slave_config(substream, params, slave_config);
	if (ret)
		return ret;

	slave_config->dst_maxburst = 32;
	slave_config->src_maxburst = 32;

	slave_config->src_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	slave_config->dst_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	/* for tmda , don't need to config dma controller addr, set to 0*/
	if (dma_id == DMA_I2S0) {
		pr_debug("i2s0_datar\n");
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
			slave_config->dst_addr = I2S0_REG_BASE + DATAR;
			slave_config->src_addr = 0;
		}
		else {
			slave_config->src_addr = I2S0_REG_BASE + DATAR;
			slave_config->dst_addr = 0;
		}
	} else if (dma_id == DMA_I2S1) {
		pr_debug("i2s1_datar\n");
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
			slave_config->dst_addr = I2S1_REG_BASE + DATAR;
			slave_config->src_addr = 0;
		}
		else {
			slave_config->src_addr = I2S1_REG_BASE + DATAR;
			slave_config->dst_addr = 0;
		}
	} else if (dma_id == DMA_HDMI) {
		pr_debug("i2s_hdmi_datar\n");
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
			slave_config->dst_addr = I2S_HDMI_REG_BASE + HDMI_TXDATA;
			slave_config->src_addr = 0;
			#ifdef HDMI_REFORMAT_ENABLE
			slave_config->src_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
			slave_config->dst_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
			#else
			slave_config->src_addr_width = DMA_SLAVE_BUSWIDTH_2_BYTES;
			slave_config->dst_addr_width = DMA_SLAVE_BUSWIDTH_2_BYTES;
			#endif
		}
		else {
			slave_config->src_addr = I2S_HDMI_REG_BASE + 0x00;
			slave_config->dst_addr = 0;
		}
	}else {
		pr_err("unsupport dma platform\n");
		return -1;
	}

	pr_debug("leave %s\n", __FUNCTION__);

	return 0;
}

static bool spacemit_get_stream_is_enable(struct spacemit_snd_soc_device *dev, int stream)
{
	bool ret;
	if (stream == SNDRV_PCM_STREAM_PLAYBACK)
		ret = dev->playback_en;
	else
		ret = dev->capture_en;
	return ret;
}

static void spacemit_update_stream_status(struct spacemit_snd_soc_device *dev, int stream, bool enable)
{
	if (stream == SNDRV_PCM_STREAM_PLAYBACK)
		dev->playback_en = enable;
	else
		dev->capture_en = enable;
	return;
}

static int spacemit_snd_pcm_hw_params(struct snd_soc_component *component, struct snd_pcm_substream *substream,
				  struct snd_pcm_hw_params *params)
{
	int ret;
	struct dma_slave_config slave_config;
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct spacemit_snd_dmadata *dmadata = runtime->private_data;

	struct snd_soc_pcm_runtime *rtd = snd_pcm_substream_chip(substream);
	struct snd_pcm *pcm = rtd->pcm;
	struct snd_pcm_substream *substream_tx;

	struct spacemit_snd_soc_device *dev = snd_soc_component_get_drvdata(component);
	struct spacemit_snd_dmadata *txdma = &dev->dmadata[0];
	struct dma_slave_config slave_config_tx;

	unsigned long flags;

	spin_lock_irqsave(&dev->lock, flags);
	pr_debug("enter %s!! allocbytes=%d, dmadata=0x%lx\n",
		__FUNCTION__, params_buffer_bytes(params), (unsigned long)dmadata);

	if (dmadata->stream == SNDRV_PCM_STREAM_PLAYBACK
			&& spacemit_get_stream_is_enable(dev, SNDRV_PCM_STREAM_CAPTURE)) {
		ret = snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(params));
		if (ret < 0)
			goto unlock;
	}
	memset(&slave_config, 0, sizeof(slave_config));
	memset(&slave_config_tx, 0, sizeof(slave_config_tx));

	if (dmadata->dma_id != DMA_I2S0 && dmadata->dma_id != DMA_I2S1) {
		pr_err("unsupport dma platform\n");
		ret = -EINVAL;
		goto unlock;
	}

	if (dmadata->stream == SNDRV_PCM_STREAM_CAPTURE
			&& !spacemit_get_stream_is_enable(dev, SNDRV_PCM_STREAM_PLAYBACK)) {
		substream_tx = pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream;
		dmadata = txdma;
		spacemit_dma_slave_config(substream_tx, params, &slave_config_tx, dmadata->dma_id);
		slave_config_tx.direction = DMA_MEM_TO_DEV;
		slave_config_tx.dst_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
		slave_config_tx.dst_addr = I2S0_REG_BASE + DATAR;
		slave_config_tx.src_addr = 0;
		ret = dmaengine_slave_config(dmadata->dma_chan, &slave_config_tx);
		if (ret)
			goto unlock;
		dmadata->substream = substream_tx;
		dmadata->pos = 0;
	}

	dmadata = runtime->private_data;
	spacemit_dma_slave_config(substream, params, &slave_config, dmadata->dma_id);

	ret = dmaengine_slave_config(dmadata->dma_chan, &slave_config);
	if (ret)
		goto unlock;

	ret = snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(params));
	if (ret < 0)
		goto unlock;

	dmadata->substream = substream;
	dmadata->pos = 0;

	pr_debug("leave %s!!\n", __FUNCTION__);
unlock:
	spin_unlock_irqrestore(&dev->lock, flags);
	if (ret < 0)
		return ret;
	return 0;
}

static int spacemit_snd_pcm_hw_free(struct snd_soc_component *component, struct snd_pcm_substream *substream)
{
	pr_debug("enter %s!!\n", __FUNCTION__);
	return snd_pcm_lib_free_pages(substream);
}

static int spacemit_snd_pcm_hdmi_hw_params(struct snd_soc_component *component, struct snd_pcm_substream *substream,
				  struct snd_pcm_hw_params *params)
{
	//config hdmi and callback
	int ret;
	struct dma_slave_config slave_config;

	struct snd_pcm_runtime *runtime = substream->runtime;
	struct spacemit_snd_dmadata *dmadata = runtime->private_data;

	pr_debug("enter %s!! allocbytes=%d, dmadata=0x%lx\n",
		__FUNCTION__, params_buffer_bytes(params), (unsigned long)dmadata);

	memset(&slave_config, 0, sizeof(slave_config));
	if (dmadata->dma_id != DMA_HDMI) {
		pr_err("unsupport adma platform\n");
		return -1;
	}
	spacemit_dma_slave_config(substream, params, &slave_config, dmadata->dma_id);

	ret = dmaengine_slave_config(dmadata->dma_chan, &slave_config);
	if (ret)
		return ret;

	ret = snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(params));
	if (ret < 0)
		return ret;

	hdmiraw_dma_area = (void *)priv.buf_base;
	hdmiraw_dma_addr = (dma_addr_t)priv.phy_addr;

	if (hdmiraw_dma_area == NULL) {
		pr_err("hdmi:raw:get mem failed...\n");
		return -ENOMEM;
	}
#ifdef HDMI_REFORMAT_ENABLE
	hdmiraw_dma_area_tmp = kzalloc((params_buffer_bytes(params) * 2), GFP_KERNEL);
	if (hdmiraw_dma_area_tmp == NULL) {
		pr_err("hdmi:raw:get mem tmp failed...\n");
		return -ENOMEM;
	}
#endif

	hdmipcm_dma_addr = substream->dma_buffer.addr;
	substream->dma_buffer.addr = (dma_addr_t)hdmiraw_dma_addr;

	dmadata->substream = substream;
	dmadata->pos = 0;
	snd_pcm_set_runtime_buffer(substream, &substream->dma_buffer);

	pr_debug("leave %s!!\n", __FUNCTION__);
	return 0;
}

static int spacemit_snd_pcm_hdmi_hw_free(struct snd_soc_component *component, struct snd_pcm_substream *substream)
{
	pr_debug("enter %s!!\n", __FUNCTION__);

	substream->dma_buffer.addr = hdmipcm_dma_addr;
	hdmiraw_dma_area = NULL;
#ifdef HDMI_REFORMAT_ENABLE
	kfree(hdmiraw_dma_area_tmp);
#endif
	return snd_pcm_lib_free_pages(substream);
}

static void spacemit_snd_hdmi_dma_complete(void *arg)
{
	struct spacemit_snd_dmadata *dmadata = arg;
	struct snd_pcm_substream *substream = dmadata->substream;
	unsigned int pos = dmadata->pos;
	struct spacemit_snd_soc_device *dev = (struct spacemit_snd_soc_device *)dmadata->private_data;
	unsigned long flags;

	spin_lock_irqsave(&dev->lock, flags);

	pos += snd_pcm_lib_period_bytes(substream);
	pos %= snd_pcm_lib_buffer_bytes(substream);
	dmadata->pos = pos;

	spin_unlock_irqrestore(&dev->lock, flags);
	if (snd_pcm_running(substream)) {
		snd_pcm_period_elapsed(substream);
	}
	return;
}

static void spacemit_snd_dma_complete(void *arg)
{
	struct spacemit_snd_dmadata *dmadata = arg;
	struct snd_pcm_substream *substream = dmadata->substream;
	struct spacemit_snd_soc_device *dev = (struct spacemit_snd_soc_device *)dmadata->private_data;
	unsigned long flags;

	spin_lock_irqsave(&dev->lock, flags);

	if (dmadata->stream == SNDRV_PCM_STREAM_PLAYBACK
		&& spacemit_get_stream_is_enable(dev, SNDRV_PCM_STREAM_CAPTURE)
		&& !spacemit_get_stream_is_enable(dev, SNDRV_PCM_STREAM_PLAYBACK)) {
			spin_unlock_irqrestore(&dev->lock, flags);
			return;
	}

	spin_unlock_irqrestore(&dev->lock, flags);
	if (snd_pcm_running(substream)) {
		snd_pcm_period_elapsed(substream);
	}
	return;
}

static int spacemit_snd_dma_submit(struct spacemit_snd_dmadata *dmadata)
{
	struct dma_async_tx_descriptor *desc;
	struct snd_pcm_substream *substream = dmadata->substream;
	struct dma_chan *chan = dmadata->dma_chan;
	unsigned long flags = DMA_CTRL_ACK;

	pr_debug("enter %s!!\n", __FUNCTION__);

	if (substream->runtime && !substream->runtime->no_period_wakeup)
		flags |= DMA_PREP_INTERRUPT;

#ifdef HDMI_REFORMAT_ENABLE
	if (dmadata->dma_id == DMA_HDMI){
		desc = dmaengine_prep_dma_cyclic(chan,
			substream->runtime->dma_addr,
			snd_pcm_lib_buffer_bytes(substream) * 2,
			snd_pcm_lib_period_bytes(substream) * 2,
			snd_pcm_substream_to_dma_direction(substream),
			flags);
	} else
#endif
	{
		if (dmadata->stream == SNDRV_PCM_STREAM_PLAYBACK){
			desc = dmaengine_prep_dma_cyclic(chan,
				substream->dma_buffer.addr,
				I2S_PERIOD_SIZE * I2S_PERIOD_COUNT * 4,
				I2S_PERIOD_SIZE * 4,
				snd_pcm_substream_to_dma_direction(substream),
				flags);
		}
		else {
			desc = dmaengine_prep_dma_cyclic(chan,
			substream->runtime->dma_addr,
			snd_pcm_lib_buffer_bytes(substream),
			snd_pcm_lib_period_bytes(substream),
			snd_pcm_substream_to_dma_direction(substream),
			flags);
		}
	}

	if (!desc)
		return -ENOMEM;

#ifdef HDMI_REFORMAT_ENABLE
	if (dmadata->dma_id == DMA_HDMI){
		desc->callback = spacemit_snd_hdmi_dma_complete;
	} else
#endif
	{
		desc->callback = spacemit_snd_dma_complete;
	}

	desc->callback_param = dmadata;
	dmadata->cookie = dmaengine_submit(desc);

	return 0;
}

static int spacemit_snd_pcm_trigger(struct snd_soc_component *component, struct snd_pcm_substream *substream, int cmd)
{
	struct spacemit_snd_dmadata *dmadata = substream->runtime->private_data;
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct spacemit_snd_soc_device *dev = snd_soc_component_get_drvdata(component);

	int ret = 0;
	struct spacemit_snd_dmadata *txdma = &dev->dmadata[0];
	unsigned long flags;

	spin_lock_irqsave(&dev->lock, flags);

	pr_debug("pcm_trigger: cmd=%d,dma_id=%d,dir=%d,p=%d,c=%d\n", cmd, dmadata->dma_id, substream->stream,
		dev->playback_en, dev->capture_en);
	if (dmadata->stream == SNDRV_PCM_STREAM_CAPTURE
		&& !spacemit_get_stream_is_enable(dev, SNDRV_PCM_STREAM_PLAYBACK)) {
		dmadata = txdma;
		switch (cmd) {
		case SNDRV_PCM_TRIGGER_START:
			memset(dmadata->substream->dma_buffer.area, 0, I2S_PERIOD_SIZE * I2S_PERIOD_COUNT * 4);
			ret = spacemit_snd_dma_submit(dmadata);
			if (ret < 0)
				goto unlock;
			dma_async_issue_pending(dmadata->dma_chan);
			dmadata->pos = 0;

			break;
		case SNDRV_PCM_TRIGGER_STOP:
			dmaengine_terminate_async(dmadata->dma_chan);
			dmadata->pos = 0;
			break;
		case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
			dmaengine_pause(dmadata->dma_chan);
			break;
		case SNDRV_PCM_TRIGGER_SUSPEND:
			if (runtime->info & SNDRV_PCM_INFO_PAUSE)
				dmaengine_pause(dmadata->dma_chan);
			else
				dmaengine_terminate_async(dmadata->dma_chan);
			break;
		case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		case SNDRV_PCM_TRIGGER_RESUME:
			dmaengine_resume(dmadata->dma_chan);
			break;
		default:
			ret = -EINVAL;
			goto unlock;
		}
		dmadata = substream->runtime->private_data;
	}

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		if (dmadata->stream == SNDRV_PCM_STREAM_PLAYBACK
			&& spacemit_get_stream_is_enable(dev, SNDRV_PCM_STREAM_CAPTURE)) {
				dmadata->playback_data = 0;
				dmadata->pos = 0;

		} else {
			ret = spacemit_snd_dma_submit(dmadata);
			if (ret < 0)
				goto unlock;
			dma_async_issue_pending(dmadata->dma_chan);
			dmadata->playback_data = 0;
			dmadata->pos = 0;
		}
		spacemit_update_stream_status(dev, dmadata->stream, true);

		break;
	case SNDRV_PCM_TRIGGER_STOP:
		if (dmadata->stream == SNDRV_PCM_STREAM_PLAYBACK
			&& spacemit_get_stream_is_enable(dev, SNDRV_PCM_STREAM_CAPTURE)) {
				dmadata->playback_data = 0;
				dmadata->pos = 0;

		} else {
			dmaengine_terminate_async(dmadata->dma_chan);
			dmadata->playback_data = 0;
			dmadata->pos = 0;
		}
		spacemit_update_stream_status(dev, dmadata->stream, false);

		break;
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		dmaengine_pause(dmadata->dma_chan);
		break;
	case SNDRV_PCM_TRIGGER_SUSPEND:
		if (runtime->info & SNDRV_PCM_INFO_PAUSE)
			dmaengine_pause(dmadata->dma_chan);
		else {
			dmaengine_terminate_async(dmadata->dma_chan);
			dmadata->playback_data = 0;
			dmadata->pos = 0;
			spacemit_update_stream_status(dev, dmadata->stream, false);
		}
		break;
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
	case SNDRV_PCM_TRIGGER_RESUME:
		dmaengine_resume(dmadata->dma_chan);
		break;
	default:
		ret = -EINVAL;
	}
unlock:
	spin_unlock_irqrestore(&dev->lock, flags);
	return ret;
}

snd_pcm_uframes_t
spacemit_snd_pcm_pointer(struct snd_soc_component *component, struct snd_pcm_substream *substream)
{
	struct spacemit_snd_dmadata *dmadata = substream->runtime->private_data;
	struct spacemit_snd_soc_device *dev = snd_soc_component_get_drvdata(component);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct dma_tx_state state;
	enum dma_status status;
	unsigned int buf_size;
	unsigned int preriod_size;
	unsigned int pos = 0;
	unsigned long flags;

	spin_lock_irqsave(&dev->lock, flags);
	status = dmaengine_tx_status(dmadata->dma_chan, dmadata->cookie, &state);
	if (status == DMA_IN_PROGRESS || status == DMA_PAUSED) {
		buf_size = I2S_PERIOD_SIZE * I2S_PERIOD_COUNT * 4;
		preriod_size = I2S_PERIOD_SIZE * 4;
		if (state.residue > 0 && state.residue <= buf_size) {
			pos = ((buf_size - state.residue) / preriod_size) * preriod_size;
		}
		runtime->delay = bytes_to_frames(runtime, state.in_flight_bytes);
	}
	if (dmadata->stream == SNDRV_PCM_STREAM_PLAYBACK
		&& spacemit_get_stream_is_enable(dev, SNDRV_PCM_STREAM_CAPTURE)
		&& spacemit_get_stream_is_enable(dev, SNDRV_PCM_STREAM_PLAYBACK)) {
		if (dmadata->playback_data == 0 && pos != 0)
			pos = 0;
		else
			dmadata->playback_data = 1;
	}
	spin_unlock_irqrestore(&dev->lock, flags);
	return bytes_to_frames(runtime, pos);
}

static snd_pcm_uframes_t
spacemit_snd_pcm_hdmi_pointer(struct snd_soc_component *component, struct snd_pcm_substream *substream)
{
	struct spacemit_snd_dmadata *dmadata = substream->runtime->private_data;
	return bytes_to_frames(substream->runtime, dmadata->pos);
}

static int spacemit_snd_pcm_open(struct snd_soc_component *component, struct snd_pcm_substream *substream)
{
	int ret = 0;
	struct spacemit_snd_soc_device *dev;
	struct spacemit_snd_dmadata *dmadata;
	struct snd_soc_pcm_runtime *rtd = snd_pcm_substream_chip(substream);
	unsigned long flags;

	pr_debug("%s enter, rtd->dev=%s,dir=%d\n", __FUNCTION__, dev_name(rtd->dev),substream->stream);

	if (!component) {
		pr_err("%s!! coundn't find component %s\n", __FUNCTION__, DRV_NAME);
		ret = -1;
		goto out;
	}

	dev = snd_soc_component_get_drvdata(component);
	if (!dev) {
		pr_err("%s!! get dev error\n", __FUNCTION__);
		ret = -1;
		goto out;
	}
	spin_lock_irqsave(&dev->lock, flags);

	dmadata = &dev->dmadata[substream->stream];
	if (dmadata->dma_id == DMA_HDMI) {
		ret = snd_soc_set_runtime_hwparams(substream, &spacemit_snd_pcm_hardware_hdmi);
	} else {
		ret = snd_soc_set_runtime_hwparams(substream, &spacemit_snd_pcm_hardware);
	}

	if (ret) {
		goto unlock;
	}

	ret = snd_pcm_hw_constraint_integer(substream->runtime, SNDRV_PCM_HW_PARAM_PERIODS);
	if (ret < 0) {
		pr_err("%s!! got serror\n", __FUNCTION__);
		goto unlock;
	}
	substream->runtime->private_data = dmadata;

	if (dmadata->dma_id == DMA_HDMI) {
        hdmi_ptr.iec_offset = 0;
        hdmi_ptr.srate = 48000;
	}
unlock:
	spin_unlock_irqrestore(&dev->lock, flags);
out:
	pr_debug("%s exit dma_id=%d\n", __FUNCTION__, dmadata->dma_id);

	return ret;
}

static int spacemit_snd_pcm_close(struct snd_soc_component *component, struct snd_pcm_substream *substream)
{
	struct spacemit_snd_dmadata *dmadata = substream->runtime->private_data;
	struct dma_chan *chan = dmadata->dma_chan;
	struct spacemit_snd_soc_device *dev = snd_soc_component_get_drvdata(component);
	unsigned long flags;

	spin_lock_irqsave(&dev->lock, flags);
	pr_debug("%s debug, dir=%d, dma_id=%d\n", __FUNCTION__,substream->stream, dmadata->dma_id);
	if (dmadata->stream == SNDRV_PCM_STREAM_PLAYBACK
		&& spacemit_get_stream_is_enable(dev, SNDRV_PCM_STREAM_CAPTURE)) {
		goto unlock;
	}
	dmaengine_terminate_all(chan);
	if (dmadata->dma_id == DMA_HDMI) {
		hdmi_ptr.iec_offset = 0;
	}
unlock:
	spin_unlock_irqrestore(&dev->lock, flags);
	return 0;
}

static int spacemit_snd_pcm_lib_ioctl(struct snd_soc_component *component, struct snd_pcm_substream *substream,
		      unsigned int cmd, void *arg)
{
	return snd_pcm_lib_ioctl(substream, cmd, arg);
}

static const char * const spacemit_pcm_dma_channel_names[] = {
	[SNDRV_PCM_STREAM_PLAYBACK] = "tx",
	[SNDRV_PCM_STREAM_CAPTURE] = "rx",
};

static int spacemit_snd_pcm_new(struct snd_soc_component *component, struct snd_soc_pcm_runtime *rtd)
{
	int i;
	int ret;
	int chan_num;

	struct spacemit_snd_soc_device *dev;
	struct snd_card *card = rtd->card->snd_card;
	struct snd_pcm *pcm = rtd->pcm;

	pr_debug("%s enter, dev=%s\n", __FUNCTION__, dev_name(rtd->dev));

	if (!component) {
		pr_err("%s: coundn't find component %s\n", __FUNCTION__, DRV_NAME);
		return -1;
	}

	dev = snd_soc_component_get_drvdata(component);
	if (!dev) {
		pr_err("%s: get dev error\n", __FUNCTION__);
		return -1;
	}
	if (dev->dmadata->dma_id == DMA_HDMI) {
		chan_num = 1;
		pr_debug("%s playback_only, dev=%s\n", __FUNCTION__, dev_name(rtd->dev));
	}else{
		chan_num = 2;
	}
	dev->dmadata[0].stream = SNDRV_PCM_STREAM_PLAYBACK;
	dev->dmadata[1].stream = SNDRV_PCM_STREAM_CAPTURE;

	for (i = 0; i < chan_num; i++) {
		ret = spacemit_snd_dma_init(component->dev, dev, &dev->dmadata[i]);
		if (ret)
			goto exit;
	}

	snd_pcm_lib_preallocate_pages_for_all(pcm, SNDRV_DMA_TYPE_DEV,
		card->dev, 64 * 1024 * 32, 4 * 1024 * 1024);

	return 0;

exit:
	for (i = 0; i < chan_num; i++) {
		if (dev->dmadata[i].dma_chan)
			dma_release_channel(dev->dmadata[i].dma_chan);
		dev->dmadata[i].dma_chan = NULL;
	}

	return ret;
}

static int spacemit_snd_dma_init(struct device *paraent, struct spacemit_snd_soc_device *dev,
			     struct spacemit_snd_dmadata *dmadata)
{
	dma_cap_mask_t mask;
	spin_lock_init(&dmadata->dma_lock);

	dma_cap_zero(mask);
	dma_cap_set(DMA_SLAVE, mask);
	dma_cap_set(DMA_CYCLIC, mask);

	dmadata->dma_chan = dma_request_slave_channel(paraent, spacemit_pcm_dma_channel_names[dmadata->stream]);
	if (!dmadata->dma_chan) {
		pr_err("DMA channel for %s is not available\n",
			dmadata->stream == SNDRV_PCM_STREAM_PLAYBACK ?
			"playback" : "capture");
		return -EBUSY;
	}

	return 0;
}

static int spacemit_snd_pcm_probe(struct snd_soc_component *component)
{
	struct spacemit_snd_soc_device *spacemit_snd_device = snd_soc_component_get_drvdata(component);

	spacemit_snd_device->dmadata[0].private_data = spacemit_snd_device;
	spacemit_snd_device->dmadata[1].private_data = spacemit_snd_device;

	return 0;
}

static void spacemit_snd_pcm_remove(struct snd_soc_component *component)
{
	int i;
	int chan_num;
	struct spacemit_snd_soc_device *dev = snd_soc_component_get_drvdata(component);

	pr_debug("%s enter\n", __FUNCTION__);

	if (dev->dmadata->dma_id == DMA_HDMI) {
		chan_num = 1;
	}else{
		chan_num = 2;
	}
	for (i = 0; i < chan_num; i++) {
		struct spacemit_snd_dmadata *dmadata = &dev->dmadata[i];
		struct dma_chan *chan = dmadata->dma_chan;

		if (chan) {
			dmaengine_terminate_all(chan);
			dma_release_channel(chan);
		}
		dev->dmadata[i].dma_chan = NULL;
	}
}

static uint32_t parity_even(uint32_t sample)
{
	bool parity = 0;
	sample ^= sample >> 16;
	sample ^= sample >> 8;
	parity = ParityTable256[sample & 0xff];
	if (parity)
		return 1;
	else
		return 0;
}

static int32_t cal_cs_status_48kHz(int32_t offset)
{
	if ((offset == CS_SAMPLING_FREQUENCY) || (offset == CS_MAX_SAMPLE_WORD))
	{
		return 1;
	} else {
		return 0;
	}
}

static void hdmi_reformat(void *dst, void *src, int len)
{
	uint32_t *dst32 = (uint32_t *)dst;
	uint16_t *src16 = (uint16_t *)src;
	struct hdmi_codec_priv *dw = &hdmi_ptr;
	uint16_t frm_cnt = len;
	uint32_t ctrl;
	uint32_t sample,parity;
	dw->channels = 2;
	while (frm_cnt--) {
		for (int i = 0; i < dw->channels; i++) {
			//bit 0-23
			sample = ((uint32_t)(*src16++) << 8);
			//bit 24
			sample = sample | (1 << VALID_OFFSET);
			//bit 26
			sample = sample | (cal_cs_status_48kHz(dw->iec_offset) << CHANNEL_STATUS_OFFSET);
			//bit 27
			parity = parity_even(sample);
			sample = sample | (parity << PARITY_BIT_OFFSET);

			//bit 30 31
			if (dw->iec_offset == 0) {
				ctrl = CS_CTRL1;
			}  else {
				ctrl = CS_CTRL2;
			}
			sample = sample | ctrl;

			*dst32++ = sample;
		}

		dw->iec_offset++;
		if (dw->iec_offset >= 192){
			dw->iec_offset = 0;
		}
	}
}

static int spacemit_snd_pcm_copy(struct snd_soc_component *component,
                          struct snd_pcm_substream *substream,
                          int channel, unsigned long hwoff,
                          struct iov_iter *iter, unsigned long bytes)
{
	int ret = 0;
	char *hwbuf;
	char *hdmihw_area;
	struct snd_pcm_runtime *runtime = substream->runtime;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
#ifdef HDMI_REFORMAT_ENABLE
		hwbuf = runtime->dma_area + hwoff;
		if (copy_from_iter(hwbuf, bytes, iter) != bytes)
                        return -EFAULT;
		hdmihw_area = hdmiraw_dma_area_tmp + 2 * hwoff;
		hdmi_reformat((int *)hdmihw_area, (short *)hwbuf, bytes_to_frames(substream->runtime, bytes));
		memcpy((void *)(hdmiraw_dma_area + 2 * hwoff), (void *)hdmihw_area, bytes * 2);
#else
		hwbuf = hdmiraw_dma_area + hwoff;
		if (hwbuf == NULL)
			pr_err("%s addr null !!!!!!!!!!!!\n", __func__);
		if (copy_from_iter(hwbuf, bytes, iter) != bytes)
                        return -EFAULT;

#endif

	} else if (substream->stream == SNDRV_PCM_STREAM_CAPTURE) {
		hwbuf = runtime->dma_area + hwoff;
		if (copy_to_iter(hwbuf, bytes, iter) != bytes)
                        return -EFAULT;
	}

	return ret;
}
static const struct snd_soc_component_driver spacemit_snd_dma_component = {
	.name          = DRV_NAME,
	.probe		   = spacemit_snd_pcm_probe,
	.remove		   = spacemit_snd_pcm_remove,
	.open		   = spacemit_snd_pcm_open,
	.close		   = spacemit_snd_pcm_close,
	.ioctl		   = spacemit_snd_pcm_lib_ioctl,
	.hw_params	   = spacemit_snd_pcm_hw_params,
	.hw_free	   = spacemit_snd_pcm_hw_free,
	.trigger	   = spacemit_snd_pcm_trigger,
	.pointer	   = spacemit_snd_pcm_pointer,
	.pcm_construct = spacemit_snd_pcm_new,
};

static const struct snd_soc_component_driver spacemit_snd_dma_component_hdmi = {
	.name          = DRV_NAME,
	.probe		   = spacemit_snd_pcm_probe,
	.remove		   = spacemit_snd_pcm_remove,
	.open		   = spacemit_snd_pcm_open,
	.close		   = spacemit_snd_pcm_close,
	.ioctl		   = spacemit_snd_pcm_lib_ioctl,
	.hw_params	   = spacemit_snd_pcm_hdmi_hw_params,
	.hw_free	   = spacemit_snd_pcm_hdmi_hw_free,
	.trigger	   = spacemit_snd_pcm_trigger,
	.pointer	   = spacemit_snd_pcm_hdmi_pointer,
	.pcm_construct = spacemit_snd_pcm_new,
	.copy		= spacemit_snd_pcm_copy,
};

static int spacemit_snd_dma_pdev_probe(struct platform_device *pdev)
{
	int ret;
	struct device_node *np = pdev->dev.of_node;
	struct resource *res;
	struct spacemit_snd_soc_device *device;

	device = devm_kzalloc(&pdev->dev, sizeof(*device), GFP_KERNEL);
	if (!device) {
		pr_err("%s: alloc memoery failed\n", __FUNCTION__);
		return -ENOMEM;
	}

	pr_debug("%s enter: dev name %s\n", __func__, dev_name(&pdev->dev));

	if (of_device_is_compatible(np, "spacemit,spacemit-snd-dma-hdmi")){
		device->dmadata->dma_id = DMA_HDMI;
		res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
		pr_debug("%s, start=0x%lx, end=0x%lx\n", __FUNCTION__, (unsigned long)res->start, (unsigned long)res->end);
		priv.phy_addr = res->start;
		priv.buf_base = devm_ioremap_resource(&pdev->dev, res);
		if (IS_ERR(priv.buf_base)) {
			pr_err("%s audio buf alloc failed\n", __FUNCTION__);
			return PTR_ERR(priv.buf_base);
		}
		ret = snd_soc_register_component(&pdev->dev, &spacemit_snd_dma_component_hdmi, NULL, 0);
	} else {
		if (of_device_is_compatible(np, "spacemit,spacemit-snd-dma0")){
			device->dmadata->dma_id = DMA_I2S0;
		} else if (of_device_is_compatible(np, "spacemit,spacemit-snd-dma1")) {
			device->dmadata->dma_id = DMA_I2S1;
		}
		ret = snd_soc_register_component(&pdev->dev, &spacemit_snd_dma_component, NULL, 0);
	}
	spin_lock_init(&device->lock);
	if (ret != 0) {
		dev_err(&pdev->dev, "failed to register DAI\n");
		return ret;
	}
	dev_set_drvdata(&pdev->dev, device);
	return 0;
}

static int spacemit_snd_dma_pdev_remove(struct platform_device *pdev)
{
	snd_soc_unregister_component(&pdev->dev);
	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id spacemit_snd_dma_ids[] = {
	{ .compatible = "spacemit,spacemit-snd-dma0", },
	{ .compatible = "spacemit,spacemit-snd-dma1", },
	{ .compatible = "spacemit,spacemit-snd-dma-hdmi", },
	{},
};
#endif

static struct platform_driver spacemit_snd_dma_pdrv = {
	.driver = {
		.name = "spacemit-snd-dma",
		.of_match_table = of_match_ptr(spacemit_snd_dma_ids),
	},
	.probe = spacemit_snd_dma_pdev_probe,
	.remove = spacemit_snd_dma_pdev_remove,
};

#if IS_MODULE(CONFIG_SND_SOC_SPACEMIT)
int spacemit_snd_register_dmaclient_pdrv(void)
{
	pr_debug("%s enter\n", __FUNCTION__);
	return platform_driver_register(&spacemit_snd_dma_pdrv);
}
EXPORT_SYMBOL(spacemit_snd_register_dmaclient_pdrv);

void spacemit_snd_unregister_dmaclient_pdrv(void)
{
	platform_driver_unregister(&spacemit_snd_dma_pdrv);
}
EXPORT_SYMBOL(spacemit_snd_unregister_dmaclient_pdrv);
#else
static int spacemit_snd_pcm_init(void)
{
	return platform_driver_register(&spacemit_snd_dma_pdrv);
}
late_initcall_sync(spacemit_snd_pcm_init);
#endif

MODULE_DESCRIPTION("SPACEMIT ASoC PCM Platform Driver");
MODULE_LICENSE("GPL");
