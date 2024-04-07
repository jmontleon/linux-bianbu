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
#include <linux/dmaengine.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/initval.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/dmaengine_pcm.h>
#include "spacemit-snd-spi.h"

static int mmp_sspa_set_dai_fmt(struct snd_soc_dai *cpu_dai, unsigned int fmt);

//APB Clock/Reset Control Register
#define APB_CLK_BASE        0xD4015000
#define APB_SSP0_CLK_RST    0x80
#define APB_SSP1_CLK_RST    0x84
#define APB_AIB_CLK_RST     0x3C

#define FNCLKSEL_6p5M       (0x0 << 4)
#define FNCLKSEL_13M        (0x1 << 4)
#define FNCLKSEL_26M        (0x2 << 4)
#define FNCLKSEL_52M        (0x3 << 4)
#define FNCLKSEL_3p25M      (0x4 << 4)
#define FNCLKSEL_1p625M     (0x5 << 4)
#define FNCLKSEL_812p5M     (0x6 << 4)
#define FNCLKSEL_AUDIO      (0x7 << 4)
#define CLK_ON_NORST        (0x3 << 0)
#define CLK_ON_RST          (0x7 << 0)
#define CLK_ON_26M          (CLK_ON_NORST | FNCLKSEL_26M)
#define CLK_ON_6p5M         (CLK_ON_NORST | FNCLKSEL_6p5M)
#define CLK_ON_I2S          ((0x1<<3) | CLK_ON_NORST | FNCLKSEL_AUDIO)

//I2S CLK
#define  PMUMAIN_BASE       0xD4050000
#define  ISCCR1             0x44
#define  SYSCLK_EN          (0x1 << 31)
#define  BITCLK_EN          (0x1 << 29)
#define  SYSCLK_BASE_156M   (0x1 << 30)
#define  SYSCLK_BASE_26M    (0x0 << 30)
#define  BITCLK_DIV_468     (0x0 << 27)
#define  FRAME_48K_I2S      (0x4 << 15)

/*
 * ssp:sspa audio private data
 */
 struct ssp_device {
	struct platform_device *pdev;
	struct list_head	node;

	struct clk	*clk;
	void __iomem	*mmio_base[2];
	void __iomem	*mmio_ctrl_base;
	void __iomem	*apb_clk_base;
	void __iomem	*pmumain;
	unsigned long	phys_base;

	const char	*label;
	int		port_id;
	int		type;
	int		use_count;
	int		irq;

	struct device_node	*of_node;
};

struct sspa_priv {
	struct ssp_device *sspa;
	struct snd_dmaengine_dai_dma_data *dma_params;
	struct reset_control *sspa0_rst;
	struct reset_control *sspa1_rst;
	int dai_fmt;
	int dai_id_pre;
	int running_cnt;
};

struct platform_device *i2splatdev;

static void mmp_sspa_write_reg(struct ssp_device *sspa, u32 reg, u32 val, int id_dai)
{
	__raw_writel(val, sspa->mmio_base[id_dai] + reg);
}

static u32 mmp_sspa_read_reg(struct ssp_device *sspa, u32 reg, int id_dai)
{
	return __raw_readl(sspa->mmio_base[id_dai] + reg);
}

static int mmp_sspa_startup(struct snd_pcm_substream *substream,
	struct snd_soc_dai *dai)
{
	pm_runtime_get_sync(&i2splatdev->dev);
	mmp_sspa_set_dai_fmt(dai, SND_SOC_DAIFMT_CBS_CFS | SND_SOC_DAIFMT_I2S);

	return 0;
}

static void mmp_sspa_shutdown(struct snd_pcm_substream *substream,
	struct snd_soc_dai *dai)
{
	pm_runtime_put_sync(&i2splatdev->dev);
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
	struct sspa_priv *sspa_priv = snd_soc_dai_get_drvdata(cpu_dai);
	struct ssp_device *sspa = sspa_priv->sspa;
	unsigned int ssp_top_cfg=0, ssp_fifo_cfg=0, ssp_int_en_cfg=0,ssp_to_cfg=0, ssp_psp_cfg=0, ssp_net_work_ctrl=0;
	int dai_id = cpu_dai->id;

	pr_debug("%s, fmt=0x%x, dai_id=0x%x\n", __FUNCTION__, fmt, dai_id);

	if ((sspa_priv->dai_fmt == fmt) & (sspa_priv->dai_id_pre == dai_id) & (mmp_sspa_read_reg(sspa, PSP_CTRL, dai_id)))
		return 0;

	ssp_top_cfg  = TOP_TRAIL_DMA | DW_32BYTE | TOP_SFRMDIR_M | TOP_SCLKDIR_M | TOP_FRF_PSP;
	ssp_fifo_cfg = FIFO_RSRE | FIFO_TSRE | FIFO_RX_THRES_15 | FIFO_TX_THRES_15;

	if ((mmp_sspa_read_reg(sspa, TOP_CTRL, dai_id) & TOP_SSE)) {
		pr_err("can't change hardware dai format: stream is in use\n");
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		ssp_top_cfg |= TOP_SFRMDIR_M;
		ssp_top_cfg |= TOP_SCLKDIR_M;
		break;
	case SND_SOC_DAIFMT_CBM_CFM:
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		ssp_top_cfg |= TOP_FRF_PSP;
		ssp_psp_cfg = (0x10<<12) | (0x1<<3) | PSP_SFRMP;
		break;
	default:
		return -EINVAL;
	}

	mmp_sspa_write_reg(sspa, TOP_CTRL, ssp_top_cfg, dai_id);
	mmp_sspa_write_reg(sspa, PSP_CTRL, ssp_psp_cfg, dai_id);
	mmp_sspa_write_reg(sspa, INT_EN, ssp_int_en_cfg, dai_id);
	mmp_sspa_write_reg(sspa, TO, ssp_to_cfg, dai_id);
	mmp_sspa_write_reg(sspa, FIFO_CTRL, ssp_fifo_cfg, dai_id);
	mmp_sspa_write_reg(sspa, NET_WORK_CTRL, ssp_net_work_ctrl, dai_id);

	pr_debug("TOP_CTRL=0x%x,\n PSP_CTRL=0x%x,\n INT_EN=0x%x,\n TO=0x%x,\n FIFO_CTRL=0x%x,\n,NET_WORK_CTRL=0x%x",
				mmp_sspa_read_reg(sspa, TOP_CTRL, dai_id),
				mmp_sspa_read_reg(sspa, PSP_CTRL, dai_id),
				mmp_sspa_read_reg(sspa, INT_EN, dai_id),
				mmp_sspa_read_reg(sspa, TO, dai_id),
				mmp_sspa_read_reg(sspa, FIFO_CTRL, dai_id),
				mmp_sspa_read_reg(sspa, NET_WORK_CTRL, dai_id));

	sspa_priv->dai_fmt = fmt;
	sspa_priv->dai_id_pre = dai_id;

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
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = asoc_rtd_to_cpu(rtd, dai->id);
	struct sspa_priv *sspa_priv = snd_soc_dai_get_drvdata(dai);
	struct ssp_device *sspa = sspa_priv->sspa;
	struct snd_dmaengine_dai_dma_data *dma_params;

	pr_debug("%s, format=0x%x\n", __FUNCTION__, params_format(params));
	dma_params = &sspa_priv->dma_params[substream->stream];
	dma_params->addr = (sspa->phys_base + DATAR);
	dma_params->maxburst = 32;
	dma_params->addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	snd_soc_dai_set_dma_data(cpu_dai, substream, dma_params);
	return 0;
}

static int mmp_sspa_trigger(struct snd_pcm_substream *substream, int cmd,
			     struct snd_soc_dai *dai)
{
	struct sspa_priv *sspa_priv = snd_soc_dai_get_drvdata(dai);
	struct ssp_device *sspa = sspa_priv->sspa;
	int ret = 0;

	pr_debug("%s cmd=%d, cnt=%d\n", __FUNCTION__, cmd, sspa_priv->running_cnt);
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		unsigned int ssp_top_cfg = mmp_sspa_read_reg(sspa, TOP_CTRL, dai->id);
		pr_debug("TOP_CTRL:0x%x", ssp_top_cfg);
		ssp_top_cfg |= TOP_SSE;
 		mmp_sspa_write_reg(sspa, TOP_CTRL, ssp_top_cfg, dai->id);   //SSP_enable
		sspa_priv->running_cnt++;
		pr_debug("triger::TOP_CTRL=0x%x,\n PSP_CTRL=0x%x,\n INT_EN=0x%x,\n TO=0x%x,\n FIFO_CTRL=0x%x,\n",
				mmp_sspa_read_reg(sspa, TOP_CTRL, dai->id), mmp_sspa_read_reg(sspa, PSP_CTRL, dai->id),
				mmp_sspa_read_reg(sspa, INT_EN, dai->id),
				mmp_sspa_read_reg(sspa, TO, dai->id),
				mmp_sspa_read_reg(sspa, FIFO_CTRL, dai->id));
		break;

	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
	if (sspa_priv->running_cnt > 0)
		sspa_priv->running_cnt--;
	if (sspa_priv->running_cnt == 0 ) {
		ssp_top_cfg = mmp_sspa_read_reg(sspa, TOP_CTRL, dai->id);
		ssp_top_cfg &= (~TOP_SSE);
		mmp_sspa_write_reg(sspa, TOP_CTRL, ssp_top_cfg, dai->id);
		pr_debug("TOP_CTRL=0x%x, dai->id=%d \n", mmp_sspa_read_reg(sspa, TOP_CTRL, dai->id), dai->id);
	}
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
			pr_debug("%s ignore playback tx\n", __FUNCTION__);
		}

		/* have no capture stream, disable rx port */
		if (!sspa_priv->running_cnt) {
			pr_debug("%s sspa_priv->running_cnt=%d\n", __FUNCTION__, sspa_priv->running_cnt);
		}
		break;

	default:
		ret = -EINVAL;
	}

	return ret;
}

static int mmp_sspa_probe(struct snd_soc_dai *dai)
{
	struct sspa_priv *priv = dev_get_drvdata(dai->dev);
	struct ssp_device *sspa = priv->sspa;
	unsigned int sspa_clk = 0;
	pr_debug("%s\n", __FUNCTION__);
	//init clock
	__raw_writel((SYSCLK_BASE_156M | BITCLK_DIV_468| FRAME_48K_I2S | 200), sspa->pmumain + ISCCR1);

	if (dai->id == 0)
	{
		//i2s0
		sspa_clk = __raw_readl(sspa->apb_clk_base + APB_SSP0_CLK_RST);
		__raw_writel((1 << 3)|sspa_clk, sspa->apb_clk_base + APB_SSP0_CLK_RST);
		reset_control_deassert(priv->sspa0_rst);
	} else {
		//i2s1
		sspa_clk = __raw_readl(sspa->apb_clk_base + APB_SSP1_CLK_RST);
		__raw_writel((1 << 3)|sspa_clk, sspa->apb_clk_base + APB_SSP1_CLK_RST);
		reset_control_deassert(priv->sspa1_rst);
    }
	snd_soc_dai_set_drvdata(dai, priv);
	return 0;

}

#define MMP_SSPA_RATES SNDRV_PCM_RATE_8000_192000
#define MMP_SSPA_FORMATS (SNDRV_PCM_FMTBIT_S8 | \
		SNDRV_PCM_FMTBIT_S16_LE | \
		SNDRV_PCM_FMTBIT_S24_LE | \
		SNDRV_PCM_FMTBIT_S32_LE)

static const struct snd_soc_dai_ops mmp_sspa_dai_ops = {
	.startup	= mmp_sspa_startup,
	.shutdown	= mmp_sspa_shutdown,
	.trigger	= mmp_sspa_trigger,
	.hw_params	= mmp_sspa_hw_params,
	.set_sysclk	= mmp_sspa_set_dai_sysclk,
	.set_pll	= mmp_sspa_set_dai_pll,
	.set_fmt	= mmp_sspa_set_dai_fmt,
};

static struct snd_soc_dai_driver mmp_sspa_dai[] = {
	{
		.name = "i2s-dai0",
		.probe = mmp_sspa_probe,
		.id = 0,
		.playback = {
			.channels_min = 1,
			.channels_max = 128,
			.rates = MMP_SSPA_RATES,
			.formats = MMP_SSPA_FORMATS,
		},
		.capture = {
			.channels_min = 1,
			.channels_max = 2,
			.rates = MMP_SSPA_RATES,
			.formats = MMP_SSPA_FORMATS,
		},
		.ops = &mmp_sspa_dai_ops,
	},
	{
		.name = "i2s-dai1",
		.probe = mmp_sspa_probe,
		.id = 1,
		.playback = {
			.channels_min = 1,
			.channels_max = 128,
			.rates = MMP_SSPA_RATES,
			.formats = MMP_SSPA_FORMATS,
		},
		.capture = {
			.channels_min = 1,
			.channels_max = 2,
			.rates = MMP_SSPA_RATES,
			.formats = MMP_SSPA_FORMATS,
		},
		.ops = &mmp_sspa_dai_ops,
	}
};

static int i2s_sspa_suspend(struct snd_soc_component *component)
{
	/*to-do */
	return 0;
}

static int i2s_sspa_resume(struct snd_soc_component *component)
{
	/*to-do */
	return 0;
}
static const struct snd_soc_component_driver mmp_sspa_component = {
	.name 			= "spacemit-dmasspa-dai",
	.resume 		= i2s_sspa_resume,
	.suspend 		= i2s_sspa_suspend,
};

static int asoc_mmp_sspa_probe(struct platform_device *pdev)
{
	struct sspa_priv *priv;
	struct resource *res;

	printk("enter %s\n", __FUNCTION__);
	priv = devm_kzalloc(&pdev->dev,
				sizeof(struct sspa_priv), GFP_KERNEL);
	if (!priv) {
		pr_err("%s priv alloc failed\n", __FUNCTION__);
		return -ENOMEM;
	}
	priv->sspa = devm_kzalloc(&pdev->dev,
				sizeof(struct ssp_device), GFP_KERNEL);
	if (priv->sspa == NULL) {
		pr_err("%s sspa alloc failed\n", __FUNCTION__);
		return -ENOMEM;
	}

	priv->dma_params = devm_kcalloc(&pdev->dev,
			2, sizeof(struct snd_dmaengine_dai_dma_data),
			GFP_KERNEL);
	if (priv->dma_params == NULL) {
		pr_err("%s dma_params alloc failed\n", __FUNCTION__);
		return -ENOMEM;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	pr_debug("%s, start=0x%lx, end=0x%lx\n", __FUNCTION__, (unsigned long)res->start, (unsigned long)res->end);
	priv->sspa->mmio_base[0] = devm_ioremap_resource(&pdev->dev, res);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	pr_debug("%s, start=0x%lx, end=0x%lx\n", __FUNCTION__, (unsigned long)res->start, (unsigned long)res->end);
	priv->sspa->mmio_base[1] = devm_ioremap_resource(&pdev->dev, res);

	if (IS_ERR(priv->sspa->mmio_base[0])) {
		pr_err("%s mmio_base[0] alloc failed\n", __FUNCTION__);
		return PTR_ERR(priv->sspa->mmio_base[0]);
	}
	if (IS_ERR(priv->sspa->mmio_base[1])) {
		pr_err("%s mmio_base[1] alloc failed\n", __FUNCTION__);
		return PTR_ERR(priv->sspa->mmio_base[1]);
	}
	if ((priv->sspa->apb_clk_base = ioremap(APB_CLK_BASE, 0x100)) == NULL) {
		pr_err("sspa ioremap err\n");
		return -1;
	}
	if ((priv->sspa->pmumain = ioremap(PMUMAIN_BASE, 0x100)) == NULL) {
		pr_err("sspa pmumain ioremap err\n");
		return -1;
	}
	//get reset
	priv->sspa0_rst = devm_reset_control_get(&pdev->dev, "sspa0-rst");
	if (IS_ERR(priv->sspa0_rst))
		return PTR_ERR(priv->sspa0_rst);

	priv->sspa1_rst = devm_reset_control_get(&pdev->dev, "sspa1-rst");
	if (IS_ERR(priv->sspa1_rst))
		return PTR_ERR(priv->sspa1_rst);

	pm_runtime_enable(&pdev->dev);
	i2splatdev = pdev;

	priv->dai_fmt = (unsigned int) -1;
	platform_set_drvdata(pdev, priv);
	pr_debug("exit %s\n", __FUNCTION__);
	return devm_snd_soc_register_component(&pdev->dev, &mmp_sspa_component,
					       mmp_sspa_dai, ARRAY_SIZE(mmp_sspa_dai));
}

static int asoc_mmp_sspa_remove(struct platform_device *pdev)
{
	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id spacemit_dmasspa_ids[] = {
	{ .compatible = "spacemit,spacemit-i2s-dai", },
	{ /* sentinel */ }
};
#endif

static struct platform_driver asoc_mmp_sspa_driver = {
	.driver = {
		.name = "dma-sspa-dai",
		.of_match_table = of_match_ptr(spacemit_dmasspa_ids),
	},
	.probe = asoc_mmp_sspa_probe,
	.remove = asoc_mmp_sspa_remove,
};

#if IS_MODULE(CONFIG_SND_SOC_SPACEMIT)
int spacemit_snd_register_i2s_pdrv(void)
{
	printk("%s\n", __FUNCTION__);
	return platform_driver_register(&asoc_mmp_sspa_driver);
}

EXPORT_SYMBOL(spacemit_snd_register_i2s_pdrv);

void spacemit_snd_unregister_i2s_pdrv(void)
{
	platform_driver_unregister(&asoc_mmp_sspa_driver);
}
EXPORT_SYMBOL(spacemit_snd_unregister_i2s_pdrv);

#else

module_platform_driver(asoc_mmp_sspa_driver);

#endif

MODULE_DESCRIPTION("I2S SSPA SoC driver");
MODULE_LICENSE("GPL");
