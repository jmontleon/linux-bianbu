// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2022 SPACEMIT Micro Limited
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/clk.h>
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
#include "spacemit-snd.h"

#define SPACEMIT_SND_SSPA_RATES SNDRV_PCM_RATE_8000_192000
#define SPACEMIT_SND_SSPA_FORMATS (SNDRV_PCM_FMTBIT_S8 | \
               SNDRV_PCM_FMTBIT_S16_LE | \
               SNDRV_PCM_FMTBIT_S24_LE | \
               SNDRV_PCM_FMTBIT_S32_LE)

static struct snd_soc_dai_driver spacemit_snd_sspa_dai[] = {
	{
		.name = "SSPA0",
		.id = SPACEMIT_SND_SSPA0,
		.playback = {
			.stream_name = "SSPA0 TX",
			.channels_min = 2,
			.channels_max = 2,
			.rates = SPACEMIT_SND_SSPA_RATES,
			.formats = SPACEMIT_SND_SSPA_FORMATS,
		},
		.capture = {
			.stream_name = "SSPA0 RX",
			.channels_min = 2,
			.channels_max = 2,
			.rates = SPACEMIT_SND_SSPA_RATES,
			.formats = SPACEMIT_SND_SSPA_FORMATS,
		},
	},
	{
		.name = "SSPA1",
		.id = SPACEMIT_SND_SSPA1,
		.playback = {
			.stream_name = "SSPA1 TX",
			.channels_min = 2,
			.channels_max = 2,
			.rates = SPACEMIT_SND_SSPA_RATES,
			.formats = SPACEMIT_SND_SSPA_FORMATS,
		},
		.capture = {
			.stream_name = "SSPA1 RX",
			.channels_min = 2,
			.channels_max = 2,
			.rates = SPACEMIT_SND_SSPA_RATES,
			.formats = SPACEMIT_SND_SSPA_FORMATS,
		},
	},
	{
		.name = "SSPA2",
		.id = SPACEMIT_SND_SSPA2,
		.playback = {
			.stream_name = "SSPA2 TX",
			.channels_min = 2,
			.channels_max = 2,
			.rates = SPACEMIT_SND_SSPA_RATES,
			.formats = SPACEMIT_SND_SSPA_FORMATS,
		},
	},

	{
		.name = "SSPA3",
		.id = SPACEMIT_SND_SSPA3,
		.playback = {
			.stream_name = "SSPA3 TX",
			.channels_min = 2,
			.channels_max = 2,
			.rates = SPACEMIT_SND_SSPA_RATES,
			.formats = SPACEMIT_SND_SSPA_FORMATS,
		},
		.capture = {
			.stream_name = "SSPA3 RX",
			.channels_min = 2,
			.channels_max = 2,
			.rates = SPACEMIT_SND_SSPA_RATES,
			.formats = SPACEMIT_SND_SSPA_FORMATS,
		},
	},
};

static const struct snd_soc_component_driver spacemit_snd_sspa_component = {
	.name		= "spacemit-snd-sspa",
};

static int spacemit_snd_sspa_pdev_probe(struct platform_device *pdev)
{
	int ret;

	dev_dbg(&pdev->dev, "enter %s\n", __FUNCTION__);
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
module_platform_driver(spacemit_snd_sspa_pdrv);
#endif

MODULE_DESCRIPTION("SPACEMIT Aquila ASoC SSPA Driver");
MODULE_LICENSE("GPL");

