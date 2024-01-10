/*
 * spacemit-wlan.c -- power on/off wlan part of SoC
 *
 * Copyright 2023, Spacemit Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
 
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/gpio.h>
#include <linux/err.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/platform_device.h>

struct wlan_pwrseq {
	struct device		*dev;
	bool power_state;
	u32 power_on_delay_ms;

	struct gpio_desc *regon;
	struct gpio_desc *hostwake;
};

static struct wlan_pwrseq *pdata = NULL;
static int spacemit_wlan_on(struct wlan_pwrseq *pwrseq, bool on_off);
static DEFINE_MUTEX(spacemit_wlan_mutex);

void spacemit_wlan_set_power(bool on_off)
{
	struct wlan_pwrseq *pwrseq = pdata;
	int ret = 0;

	if (!pwrseq)
		return;

	mutex_lock(&spacemit_wlan_mutex);
	if (on_off != pwrseq->power_state) {
		ret = spacemit_wlan_on(pwrseq, on_off);
		if (ret)
			dev_err(pwrseq->dev, "set power failed\n");
	}

	mutex_unlock(&spacemit_wlan_mutex);
}
EXPORT_SYMBOL_GPL(spacemit_wlan_set_power);

int spacemit_wlan_get_oob_irq(void)
{
	struct wlan_pwrseq *pwrseq = pdata;
	int host_oob_irq = 0;

	if (!pwrseq || IS_ERR(pwrseq->hostwake))
		return 0;

	host_oob_irq = gpiod_to_irq(pwrseq->hostwake);
	if (host_oob_irq < 0)
		dev_err(pwrseq->dev, "map hostwake gpio to virq failed\n");

	return host_oob_irq;
}
EXPORT_SYMBOL_GPL(spacemit_wlan_get_oob_irq);

int spacemit_wlan_get_oob_irq_flags(void)
{
	struct wlan_pwrseq *pwrseq = pdata;
	int oob_irq_flags;

	if (!pwrseq)
		return 0;

	oob_irq_flags = (IRQF_TRIGGER_HIGH | IRQF_SHARED | IRQF_NO_SUSPEND);

	return oob_irq_flags;
}
EXPORT_SYMBOL_GPL(spacemit_wlan_get_oob_irq_flags);

static int spacemit_wlan_on(struct wlan_pwrseq *pwrseq, bool on_off)
{
	if (!pwrseq || IS_ERR(pwrseq->regon))
		return 0;

	if (on_off){
		gpiod_set_value(pwrseq->regon, 1);
		if (pwrseq->power_on_delay_ms)
			msleep(pwrseq->power_on_delay_ms);
	}else{
		gpiod_set_value(pwrseq->regon, 0);
	}

	pwrseq->power_state = on_off;
	return 0;
}

static int spacemit_wlan_probe(struct platform_device *pdev)
{
	struct device		*dev = &pdev->dev;
	struct wlan_pwrseq *pwrseq;

	pwrseq = devm_kzalloc(dev, sizeof(*pwrseq), GFP_KERNEL);
	if (!pwrseq)
		return -ENOMEM;

	pwrseq->dev = dev;
	pdata = pwrseq;
	platform_set_drvdata(pdev, pwrseq);

	pwrseq->regon = devm_gpiod_get_index(dev, "regon",
							0, GPIOD_OUT_LOW);
	if (IS_ERR(pwrseq->regon) &&
		PTR_ERR(pwrseq->regon) != -ENOENT &&
		PTR_ERR(pwrseq->regon) != -ENOSYS) {
		return PTR_ERR(pwrseq->regon);
	}

	pwrseq->hostwake = devm_gpiod_get_index(dev, "hostwake",
							1, GPIOD_IN);
	if (IS_ERR(pwrseq->hostwake) &&
		PTR_ERR(pwrseq->hostwake) != -ENOENT &&
		PTR_ERR(pwrseq->hostwake) != -ENOSYS) {
		return PTR_ERR(pwrseq->hostwake);
	}

	device_property_read_u32(dev, "power-on-delay-ms",
				 &pwrseq->power_on_delay_ms);
	return 0;
}

static int spacemit_wlan_remove(struct platform_device *pdev)
{
	return 0;
}

static const struct of_device_id spacemit_wlan_ids[] = {
	{ .compatible = "spacemit,wlan-pwrseq" },
	{ /* Sentinel */ }
};

#ifdef CONFIG_PM_SLEEP
static int spacemit_wlan_suspend(struct device *dev)
{
	return 0;
}

static int spacemit_wlan_resume(struct device *dev)
{
	return 0;
}

static const struct dev_pm_ops spacemit_wlan_dev_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(spacemit_wlan_suspend, spacemit_wlan_resume)
};

#define DEV_PM_OPS	(&spacemit_wlan_dev_pm_ops)
#else
#define DEV_PM_OPS	NULL
#endif /* CONFIG_PM_SLEEP */

static struct platform_driver spacemit_wlan_driver = {
	.probe		= spacemit_wlan_probe,
	.remove	= spacemit_wlan_remove,
	.driver	= {
		.owner	= THIS_MODULE,
		.name	= "spacemit-wlan",
		.of_match_table	= spacemit_wlan_ids,
	},
};

module_platform_driver(spacemit_wlan_driver);

MODULE_DESCRIPTION("spacemit wlan pwrseq driver");
MODULE_LICENSE("GPL");
