// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2024 SPACEMIT
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/reset.h>
#include <linux/pm_runtime.h>
#include <linux/pm.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/of.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/initval.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/pxa2xx-lib.h>
#include <sound/dmaengine_pcm.h>
#include "spacemit-snd-sspa.h"

struct sspa_priv {
	struct ssp_device *sspa;
	struct snd_dmaengine_dai_dma_data *dma_params;
	struct reset_control *rst;
	int dai_fmt;
	int dai_id_pre;
	int running_cnt;
	void __iomem	*base;
	void __iomem	*base_clk;
	void __iomem	*base_hdmi;
};

struct platform_device *sspa_platdev;

static int mmp_sspa_startup(struct snd_pcm_substream *substream,
	struct snd_soc_dai *dai)
{
	u32 value = 0;
	struct sspa_priv *sspa_priv = snd_soc_dai_get_drvdata(dai);

	value = readl_relaxed(sspa_priv->base_hdmi);
	value |= BIT(0);
	writel(value, sspa_priv->base_hdmi);

	pm_runtime_get_sync(&sspa_platdev->dev);
	return 0;
}

static void mmp_sspa_shutdown(struct snd_pcm_substream *substream,
	struct snd_soc_dai *dai)
{
	u32 value = 0;
	struct sspa_priv *sspa_priv = snd_soc_dai_get_drvdata(dai);

	value = readl_relaxed(sspa_priv->base_hdmi);
	value &= ~BIT(0);
	writel(value, sspa_priv->base_hdmi);

	pm_runtime_put_sync(&sspa_platdev->dev);
}

static int mmp_sspa_set_dai_sysclk(struct snd_soc_dai *cpu_dai,
				    int clk_id, unsigned int freq, int dir)
{
	return 0;
}

static int mmp_sspa_set_dai_pll(struct snd_soc_dai *cpu_dai, int pll_id,
				 int source, unsigned int freq_in,
				 unsigned int freq_out)
{
	return 0;
}

/*
 * Set up the sspa dai format. The sspa port must be inactive
 * before calling this function as the physical
 * interface format is changed.
 */
static int mmp_sspa_set_dai_fmt(struct snd_soc_dai *cpu_dai,
				 unsigned int fmt)
{
	return 0;
}

/*
 * Set the SSPA audio DMA parameters and sample size.
 * Can be called multiple times.
 */
static int mmp_sspa_hw_params(struct snd_pcm_substream *substream,
			       struct snd_pcm_hw_params *params,
			       struct snd_soc_dai *dai)
{
	struct sspa_priv *sspa_priv = snd_soc_dai_get_drvdata(dai);
	struct snd_dmaengine_dai_dma_data *dma_params;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		pr_debug("%s, format=0x%x\n", __FUNCTION__, params_format(params));
		dma_params = sspa_priv->dma_params;
		dma_params->maxburst = 32;
		dma_params->addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
		snd_soc_dai_set_dma_data(dai, substream, dma_params);
	}
	return 0;
}

static int mmp_sspa_trigger(struct snd_pcm_substream *substream, int cmd,
			     struct snd_soc_dai *dai)
{
	struct sspa_priv *sspa_priv = snd_soc_dai_get_drvdata(dai);
	int ret = 0;

	pr_debug("%s cmd=%d, cnt=%d\n", __FUNCTION__, cmd, sspa_priv->running_cnt);
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		sspa_priv->running_cnt++;
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		if (sspa_priv->running_cnt > 0)
			sspa_priv->running_cnt--;
		break;
	default:
		ret = -EINVAL;
	}
	return ret;
}

static int mmp_sspa_probe(struct snd_soc_dai *dai)
{
	struct sspa_priv *sspa_priv = dev_get_drvdata(dai->dev);
	pr_debug("%s\n", __FUNCTION__);

	snd_soc_dai_set_drvdata(dai, sspa_priv);
	return 0;
}

static const struct snd_soc_dai_ops mmp_sspa_dai_ops = {
	.probe		= mmp_sspa_probe,
	.startup	= mmp_sspa_startup,
	.shutdown	= mmp_sspa_shutdown,
	.trigger	= mmp_sspa_trigger,
	.hw_params	= mmp_sspa_hw_params,
	.set_sysclk	= mmp_sspa_set_dai_sysclk,
	.set_pll	= mmp_sspa_set_dai_pll,
	.set_fmt	= mmp_sspa_set_dai_fmt,
};

#define SPACEMIT_SND_SSPA_RATES SNDRV_PCM_RATE_48000
#define SPACEMIT_SND_SSPA_FORMATS SNDRV_PCM_FMTBIT_S16_LE

static struct snd_soc_dai_driver spacemit_snd_sspa_dai[] = {
	{
		.name = "SSPA2",
		.playback = {
			.stream_name = "SSPA2 TX",
			.channels_min = 2,
			.channels_max = 2,
			.rates = SPACEMIT_SND_SSPA_RATES,
			.formats = SPACEMIT_SND_SSPA_FORMATS,
		},
		.ops = &mmp_sspa_dai_ops,
	},
};

static void spacemit_dma_params_init(struct resource *res, struct snd_dmaengine_dai_dma_data *dma_params)
{
	dma_params->addr = res->start + 0x80;
	dma_params->maxburst = 32;
	dma_params->addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
}

static int spacemit_sspa_suspend(struct snd_soc_component *component)
{
	struct sspa_priv *priv = snd_soc_component_get_drvdata(component);
	reset_control_assert(priv->rst);
	return 0;
}

static int spacemit_sspa_resume(struct snd_soc_component *component)
{
	struct sspa_priv *priv = snd_soc_component_get_drvdata(component);
	u32 value = 0;
	value = readl_relaxed(priv->base_hdmi);
	value |= BIT(0);
	writel(value, priv->base_hdmi);
	reset_control_deassert(priv->rst);
	return 0;
}
static const struct snd_soc_component_driver spacemit_snd_sspa_component = {
	.name		= "spacemit-snd-sspa",
	.suspend	= spacemit_sspa_suspend,
	.resume		= spacemit_sspa_resume,
};

static int spacemit_snd_sspa_pdev_probe(struct platform_device *pdev)
{
	int ret;
	struct sspa_priv *priv;
	struct resource *base_res;
	struct resource *clk_res;

	pr_info("enter %s\n", __FUNCTION__);
	priv = devm_kzalloc(&pdev->dev,
				sizeof(struct sspa_priv), GFP_KERNEL);
	if (!priv) {
		pr_err("%s priv alloc failed\n", __FUNCTION__);
		return -ENOMEM;
	}
	base_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	priv->base = devm_ioremap_resource(&pdev->dev, base_res);
	if (IS_ERR(priv->base)) {
		pr_err("%s reg base alloc failed\n", __FUNCTION__);
		return PTR_ERR(priv->base);
	}
	clk_res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	priv->base_clk = devm_ioremap_resource(&pdev->dev, clk_res);
	if (IS_ERR(priv->base_clk)) {
		pr_err("%s reg clk base alloc failed\n", __FUNCTION__);
		return PTR_ERR(priv->base_clk);
	}
	priv->base_hdmi = (void __iomem *)ioremap(0xC0400530, 1);
	if (IS_ERR(priv->base_hdmi)) {
		pr_err("%s reg hdmi base alloc failed\n", __FUNCTION__);
		return PTR_ERR(priv->base_hdmi);
	}
	priv->dma_params = devm_kzalloc(&pdev->dev, sizeof(struct snd_dmaengine_dai_dma_data),
			GFP_KERNEL);
	if (priv->dma_params == NULL) {
		pr_err("%s dma_params alloc failed\n", __FUNCTION__);
		return -ENOMEM;
	}
	spacemit_dma_params_init(base_res, priv->dma_params);

	//get reset
	priv->rst = devm_reset_control_get(&pdev->dev, NULL);
	if (IS_ERR(priv->rst))
		return PTR_ERR(priv->rst);

	reset_control_deassert(priv->rst);

	pm_runtime_enable(&pdev->dev);

	sspa_platdev = pdev;

	platform_set_drvdata(pdev, priv);
	ret = devm_snd_soc_register_component(&pdev->dev, &spacemit_snd_sspa_component,
						   spacemit_snd_sspa_dai, ARRAY_SIZE(spacemit_snd_sspa_dai));
	if (ret != 0) {
		dev_err(&pdev->dev, "failed to register DAI\n");
		return ret;
	}
	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id spacemit_snd_sspa_ids[] = {
	{ .compatible = "spacemit,spacemit-snd-sspa", },
	{ /* sentinel */ }
};
#endif

static struct platform_driver spacemit_snd_sspa_pdrv = {
	.driver = {
		.name = "spacemit-snd-sspa",
		.of_match_table = of_match_ptr(spacemit_snd_sspa_ids),
	},
	.probe = spacemit_snd_sspa_pdev_probe,
};

#if IS_MODULE(CONFIG_SND_SOC_SPACEMIT)
int spacemit_snd_register_sspa_pdrv(void)
{
	return platform_driver_register(&spacemit_snd_sspa_pdrv);
}
EXPORT_SYMBOL(spacemit_snd_register_sspa_pdrv);

void spacemit_snd_unregister_sspa_pdrv(void)
{
	platform_driver_unregister(&spacemit_snd_sspa_pdrv);
}
EXPORT_SYMBOL(spacemit_snd_unregister_sspa_pdrv);
#else
static int spacemit_snd_sspa_init(void)
{
	return platform_driver_register(&spacemit_snd_sspa_pdrv);
}
late_initcall_sync(spacemit_snd_sspa_init);
#endif

MODULE_DESCRIPTION("SPACEMIT ASoC SSPA Driver");
MODULE_LICENSE("GPL");

