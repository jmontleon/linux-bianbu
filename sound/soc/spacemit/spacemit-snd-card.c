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

__maybe_unused SND_SOC_DAILINK_DEF(es8326,
	DAILINK_COMP_ARRAY(COMP_CODEC("es8326.2-0019", "ES8326 HiFi")));

struct snd_soc_jack jack;
static int spacemit_jack_init(struct snd_soc_pcm_runtime *rtd)
{
	int ret;
	struct snd_soc_component *component = asoc_rtd_to_codec(rtd, 0)->component;

	ret = snd_soc_card_jack_new_pins(rtd->card, "Headset Jack",
			SND_JACK_HEADSET | SND_JACK_BTN_0 |
			SND_JACK_BTN_1 | SND_JACK_BTN_2, &jack,
			NULL, 0);
	if (ret) {
		dev_err(rtd->dev, "Headset Jack creation failed %d\n", ret);
		return ret;
	}

	snd_jack_set_key(jack.jack, SND_JACK_BTN_0, KEY_PLAYPAUSE);
	snd_jack_set_key(jack.jack, SND_JACK_BTN_1, KEY_VOLUMEUP);
	snd_jack_set_key(jack.jack, SND_JACK_BTN_2, KEY_VOLUMEDOWN);

	snd_soc_component_set_jack(component, &jack, NULL);

	return ret;
}

static struct snd_soc_dai_link spacemit_snd_es8326_dai_links[] = {
	{
		.name = "AP SSPA0 PCM",
		.stream_name = "AP SSPA0 Playback/Capture",
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF
			| SND_SOC_DAIFMT_CBM_CFM,
		.init = spacemit_jack_init,
		SND_SOC_DAILINK_REG(i2s0, es8326, pcm_dma0)
	},
};

static struct snd_soc_card spacemit_snd_card_es8326 = {
	.name = "snd-es8326",
	.owner = THIS_MODULE,
};

static struct snd_soc_dai_link spacemit_snd_hdmi_dai_links[] = {
	{
		.name = "ADSP SSPA2 PCM",
		.stream_name = "ADSP SSPA2 Playback",
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF
			| SND_SOC_DAIFMT_CBM_CFM,
		SND_SOC_DAILINK_REG(sspa2, dummy, pcm_dma_hdmi)
	},
};

static struct snd_soc_card spacemit_snd_card_hdmi = {
	.name = "snd-hdmi",
	.owner = THIS_MODULE,
};

static int spacemit_snd_pdev_probe(struct platform_device *pdev)
{
	int ret;
	struct snd_soc_card *spacemit_snd_card;
	pr_debug("enter %s\n", __func__);

	if (of_device_is_compatible(pdev->dev.of_node, "spacemit,spacemit-snd-hdmi")){
		spacemit_snd_card = &spacemit_snd_card_hdmi;
		spacemit_snd_card->dai_link = &spacemit_snd_hdmi_dai_links[0];
		spacemit_snd_card->num_links = ARRAY_SIZE(spacemit_snd_hdmi_dai_links);
	} else if (of_device_is_compatible(pdev->dev.of_node, "spacemit,spacemit-snd-es8326")){
		spacemit_snd_card = &spacemit_snd_card_es8326;
		spacemit_snd_card->dai_link = &spacemit_snd_es8326_dai_links[0];
		spacemit_snd_card->num_links = ARRAY_SIZE(spacemit_snd_es8326_dai_links);
	} else {
		return 0;
	}
	spacemit_snd_card->dev = &pdev->dev;
	platform_set_drvdata(pdev, spacemit_snd_card);

	ret =  devm_snd_soc_register_card(&pdev->dev, spacemit_snd_card);
	return ret;
}

static struct of_device_id spacemit_snd_dt_ids[] = {
	{.compatible = "spacemit,spacemit-snd-hdmi",},
	{.compatible = "spacemit,spacemit-snd-es8326",},
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

