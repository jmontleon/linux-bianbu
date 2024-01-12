// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2022 SPACEMIT Micro Limited
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
#include <soc/spacemit/plat.h>

#include "spacemit-snd.h"
#include "spacemit-snd-soc.h"

SND_SOC_DAILINK_DEF(dummy,
	DAILINK_COMP_ARRAY(COMP_DUMMY()));

SND_SOC_DAILINK_DEF(sspa0,
	DAILINK_COMP_ARRAY(COMP_CPU("SSPA0")));

SND_SOC_DAILINK_DEF(sspa1,
	DAILINK_COMP_ARRAY(COMP_CPU("SSPA1")));

SND_SOC_DAILINK_DEF(sspa2,
	DAILINK_COMP_ARRAY(COMP_CPU("SSPA2")));

SND_SOC_DAILINK_DEF(sspa3,
	DAILINK_COMP_ARRAY(COMP_CPU("SSPA3")));

SND_SOC_DAILINK_DEF(pcm,
	DAILINK_COMP_ARRAY(COMP_PLATFORM("spacemit-snd-pcm")));

SND_SOC_DAILINK_DEF(i2s0,
	DAILINK_COMP_ARRAY(COMP_CPU("i2s-dai0")));

SND_SOC_DAILINK_DEF(i2s1,
	DAILINK_COMP_ARRAY(COMP_CPU("i2s-dai1")));

SND_SOC_DAILINK_DEF(pcm_dma0,
	DAILINK_COMP_ARRAY(COMP_PLATFORM("spacemit-snd-dma0")));

SND_SOC_DAILINK_DEF(pcm_dma1,
	DAILINK_COMP_ARRAY(COMP_PLATFORM("spacemit-snd-dma1")));

static int spacemit_be_audcodec_fixup(struct snd_soc_pcm_runtime *rtd,
  				      struct snd_pcm_hw_params *params)
{
	struct snd_interval *rate = hw_param_interval(params,
					SNDRV_PCM_HW_PARAM_RATE);
	struct snd_interval *channels = hw_param_interval(params,
					SNDRV_PCM_HW_PARAM_CHANNELS);

	rate->min = rate->max = 48000;
	channels->min = channels->max = 2;

	return 0;
}

struct snd_soc_jack jack;

static int spacemit_audcodec_init(struct snd_soc_pcm_runtime *rtd)
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

static struct snd_soc_dai_link spacemit_snd_dai_links[] = {
	{
		.name = "ADSP SSPA0 PCM",
		.stream_name = "ADSP SSPA0 Playback/Capture",
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF
			| SND_SOC_DAIFMT_CBM_CFM,
		.be_hw_params_fixup = spacemit_be_audcodec_fixup,
  		.init = spacemit_audcodec_init,
		SND_SOC_DAILINK_REG(sspa0, dummy, pcm)
	},
	{
		.name = "ADSP SSPA1 PCM",
		.stream_name = "ADSP SSPA1 Playback/Capture",
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF
			| SND_SOC_DAIFMT_CBM_CFM,
		SND_SOC_DAILINK_REG(sspa1, dummy, pcm)
	},
	{
		.name = "ADSP SSPA2 PCM",
		.stream_name = "ADSP SSPA2 Playback",
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF
			| SND_SOC_DAIFMT_CBM_CFM,
		SND_SOC_DAILINK_REG(sspa2, dummy, pcm)
	},
	{
		.name = "ADSP SSPA3 PCM",
		.stream_name = "ADSP SSPA3 Playback/Capture",
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF
			| SND_SOC_DAIFMT_CBM_CFM,
		SND_SOC_DAILINK_REG(sspa3, dummy, pcm)
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
	//spacemit_snd_dai_links[0].codecs->name = "c0882000.spacemit-audio-codec";
	//spacemit_snd_dai_links[0].codecs->dai_name = "spacemit-audcodec";
	spacemit_snd_card.dev = &pdev->dev;
	spacemit_snd_card.dai_link = &spacemit_snd_dai_links[0];
	spacemit_snd_card.num_links = ARRAY_SIZE(spacemit_snd_dai_links);

	spacemit_snd_card.late_probe = spacemit_snd_soc_init;
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

#if IS_MODULE(CONFIG_SND_SOC_spacemit)
extern int spacemit_snd_register_sspa_pdrv(void);
extern int spacemit_snd_register_pcm_pdrv(void);
extern int spacemit_snd_register_dmaclient_pdrv(void);
extern int spacemit_snd_register_i2s_pdrv(void);

extern void spacemit_snd_unregister_sspa_pdrv(void);
extern void spacemit_snd_unregister_pcm_pdrv(void);
extern void spacemit_snd_unregister_dmaclient_pdrv(void);
extern void spacemit_snd_unregister_i2s_pdrv(void);

static int __init spacemit_snd_init(void)
{
	int ret = 0;

	ret = spacemit_snd_register_sspa_pdrv();
	if (ret)
		goto exit;

	ret = spacemit_snd_register_pcm_pdrv();
	if (ret)
		goto exit;

	ret = spacemit_snd_register_dmaclient_pdrv();
	if (ret)
		goto exit;

	ret = spacemit_snd_register_i2s_pdrv();
	if (ret)
		goto exit;
	ret = platform_driver_register(&spacemit_snd_pdrv);

exit:
	return ret;
}
module_init(spacemit_snd_init);

static void __exit spacemit_snd_exit(void)
{
	platform_driver_unregister(&spacemit_snd_pdrv);
	spacemit_snd_unregister_pcm_pdrv();
	spacemit_snd_unregister_sspa_pdrv();
	spacemit_snd_unregister_dmaclient_pdrv();
	spacemit_snd_unregister_i2s_pdrv();
}
module_exit(spacemit_snd_exit);
#else
module_platform_driver(spacemit_snd_pdrv);
#endif

/* Module information */
MODULE_DESCRIPTION("SPACEMIT ASoC Machine Driver");
MODULE_LICENSE("GPL");

