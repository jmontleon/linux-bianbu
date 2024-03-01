// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2024 SPACEMIT
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/jack.h>
#include <sound/soc.h>
#include <linux/io.h>
#include <linux/uaccess.h>
#include <linux/input.h>

__maybe_unused SND_SOC_DAILINK_DEF(dummy,
	DAILINK_COMP_ARRAY(COMP_DUMMY()));

__maybe_unused SND_SOC_DAILINK_DEF(sspa2,
	DAILINK_COMP_ARRAY(COMP_CPU("SSPA2")));

__maybe_unused SND_SOC_DAILINK_DEF(i2s0,
	DAILINK_COMP_ARRAY(COMP_CPU("i2s-dai0")));

__maybe_unused SND_SOC_DAILINK_DEF(i2s1,
	DAILINK_COMP_ARRAY(COMP_CPU("i2s-dai1")));

__maybe_unused SND_SOC_DAILINK_DEF(pcm_dma0,
	DAILINK_COMP_ARRAY(COMP_PLATFORM("spacemit-snd-dma0")));

__maybe_unused SND_SOC_DAILINK_DEF(pcm_dma1,
	DAILINK_COMP_ARRAY(COMP_PLATFORM("spacemit-snd-dma1")));

__maybe_unused SND_SOC_DAILINK_DEF(pcm_dma_hdmi,
	DAILINK_COMP_ARRAY(COMP_PLATFORM("c08d0400.spacemit-snd-dma-hdmi")));

static struct snd_soc_dai_link spacemit_snd_dai_links[] = {
	{
		.name = "ADSP SSPA2 PCM",
		.stream_name = "ADSP SSPA2 Playback",
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF
			| SND_SOC_DAIFMT_CBM_CFM,
		SND_SOC_DAILINK_REG(sspa2, dummy, pcm_dma_hdmi)
	},

};


static struct snd_soc_card spacemit_snd_card = {
	.name = "spacemit-snd",
	.owner = THIS_MODULE,
};

static int spacemit_snd_pdev_probe(struct platform_device *pdev)
{
	int ret;
	pr_debug("enter %s\n", __func__);
	spacemit_snd_card.dev = &pdev->dev;
	spacemit_snd_card.dai_link = &spacemit_snd_dai_links[0];
	spacemit_snd_card.num_links = ARRAY_SIZE(spacemit_snd_dai_links);

	printk("spacemit %s\n", __func__);
	platform_set_drvdata(pdev, &spacemit_snd_card);

	ret =  devm_snd_soc_register_card(&pdev->dev, &spacemit_snd_card);
	printk("spacemit %s, register card ret = %d\n", __func__,ret);
	return ret;
}

static struct of_device_id spacemit_snd_dt_ids[] = {
	{.compatible = "spacemit,spacemit-snd",},
	{}
};

static struct platform_driver spacemit_snd_pdrv = {
	.probe = spacemit_snd_pdev_probe,
	.driver = {
		.name = "spacemit-snd",
		.of_match_table = spacemit_snd_dt_ids,
		.pm = &snd_soc_pm_ops,
	},
};

#if IS_MODULE(CONFIG_SND_SOC_SPACEMIT)
static int __init spacemit_snd_init(void)
{
	int ret = 0;
	ret = platform_driver_register(&spacemit_snd_pdrv);
	return ret;
}
module_init(spacemit_snd_init);

static void __exit spacemit_snd_exit(void)
{
	platform_driver_unregister(&spacemit_snd_pdrv);
}
module_exit(spacemit_snd_exit);
#else
module_platform_driver(spacemit_snd_pdrv);
#endif

/* Module information */
MODULE_DESCRIPTION("SPACEMIT ASoC Machine Driver");
MODULE_LICENSE("GPL");

