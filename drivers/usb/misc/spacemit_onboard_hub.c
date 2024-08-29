// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Onboard USB Hub support for Spacemit platform
 *
 * Copyright (c) 2023 Spacemit Inc.
 */

#include <linux/kernel.h>
#include <linux/resource.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/of_device.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/device.h>
#include <linux/of_address.h>
#include <linux/gpio/consumer.h>

#include "spacemit_onboard_hub.h"

#define DRIVER_VERSION "v1.0.2"

static void spacemit_hub_enable(struct spacemit_hub_priv *spacemit, bool on)
{
	unsigned i;
	int active_val = spacemit->hub_gpio_active_low ? 0 : 1;

	if (!spacemit->hub_gpios)
		return;

	dev_dbg(spacemit->dev, "do hub enable %s\n", on ? "on" : "off");

	if (on) {
		for (i = 0; i < spacemit->hub_gpios->ndescs; i++) {
			gpiod_set_value(spacemit->hub_gpios->desc[i],
					active_val);
			if (spacemit->hub_inter_delay_ms) {
				msleep(spacemit->hub_inter_delay_ms);
			}
		}
	} else {
		for (i = spacemit->hub_gpios->ndescs; i > 0; --i) {
			gpiod_set_value(spacemit->hub_gpios->desc[i - 1],
					!active_val);
			if (spacemit->hub_inter_delay_ms) {
				msleep(spacemit->hub_inter_delay_ms);
			}
		}
	}
	spacemit->is_hub_on = on;
}

static void spacemit_hub_vbus_enable(struct spacemit_hub_priv *spacemit,
					 bool on)
{
	unsigned i;
	int active_val = spacemit->vbus_gpio_active_low ? 0 : 1;

	if (!spacemit->vbus_gpios)
		return;

	dev_dbg(spacemit->dev, "do hub vbus on %s\n", on ? "on" : "off");
	if (on) {
		for (i = 0; i < spacemit->vbus_gpios->ndescs; i++) {
			gpiod_set_value(spacemit->vbus_gpios->desc[i],
					active_val);
			if (spacemit->vbus_inter_delay_ms) {
				msleep(spacemit->vbus_inter_delay_ms);
			}
		}
	} else {
		for (i = spacemit->vbus_gpios->ndescs; i > 0; --i) {
			gpiod_set_value(spacemit->vbus_gpios->desc[i - 1],
					!active_val);
			if (spacemit->vbus_inter_delay_ms) {
				msleep(spacemit->vbus_inter_delay_ms);
			}
		}
	}
	spacemit->is_vbus_on = on;
}

static void spacemit_hub_configure(struct spacemit_hub_priv *spacemit, bool on)
{
	dev_dbg(spacemit->dev, "do hub configure %s\n", on ? "on" : "off");
	if (on) {
		spacemit_hub_enable(spacemit, true);
		if (spacemit->vbus_delay_ms && spacemit->vbus_gpios) {
			msleep(spacemit->vbus_delay_ms);
		}
		spacemit_hub_vbus_enable(spacemit, true);
	} else {
		spacemit_hub_vbus_enable(spacemit, false);
		if (spacemit->vbus_delay_ms && spacemit->vbus_gpios) {
			msleep(spacemit->vbus_delay_ms);
		}
		spacemit_hub_enable(spacemit, false);
	}
}

static void spacemit_read_u32_prop(struct device *dev, const char *name,
				   u32 init_val, u32 *pval)
{
	if (device_property_read_u32(dev, name, pval))
		*pval = init_val;
	dev_dbg(dev, "hub %s, delay: %u ms\n", name, *pval);
}

static int spacemit_hub_probe(struct platform_device *pdev)
{
	struct spacemit_hub_priv *spacemit;
	struct device *dev = &pdev->dev;
	int ret;

	dev_info(&pdev->dev, "%s\n", DRIVER_VERSION);

	spacemit = devm_kzalloc(&pdev->dev, sizeof(*spacemit), GFP_KERNEL);
	if (!spacemit)
		return -ENOMEM;

	spacemit_read_u32_prop(dev, "hub_inter_delay_ms", 0,
				   &spacemit->hub_inter_delay_ms);
	spacemit_read_u32_prop(dev, "vbus_inter_delay_ms", 0,
				   &spacemit->vbus_inter_delay_ms);
	spacemit_read_u32_prop(dev, "vbus_delay_ms", 10,
				   &spacemit->vbus_delay_ms);

	spacemit->hub_gpio_active_low =
		device_property_read_bool(dev, "hub_gpio_active_low");
	spacemit->vbus_gpio_active_low =
		device_property_read_bool(dev, "vbus_gpio_active_low");
	spacemit->suspend_power_on =
		device_property_read_bool(dev, "suspend_power_on");

	pm_runtime_enable(dev);
	pm_runtime_get_noresume(dev);
	pm_runtime_get_sync(dev);

	spacemit->hub_gpios = devm_gpiod_get_array_optional(
		&pdev->dev, "hub",
		spacemit->hub_gpio_active_low ? GPIOD_OUT_HIGH : GPIOD_OUT_LOW);
	if (IS_ERR(spacemit->hub_gpios)) {
		dev_err(&pdev->dev, "failed to retrieve hub-gpios from dts\n");
		ret = PTR_ERR(spacemit->hub_gpios);
		goto err_rpm;
	}

	spacemit->vbus_gpios = devm_gpiod_get_array_optional(
		&pdev->dev, "vbus",
		spacemit->vbus_gpio_active_low ? GPIOD_OUT_HIGH : GPIOD_OUT_LOW);
	if (IS_ERR(spacemit->vbus_gpios)) {
		dev_err(&pdev->dev, "failed to retrieve vbus-gpios from dts\n");
		ret = PTR_ERR(spacemit->vbus_gpios);
		goto err_rpm;
	}

	platform_set_drvdata(pdev, spacemit);
	spacemit->dev = &pdev->dev;
	mutex_init(&spacemit->hub_mutex);

	spacemit_hub_configure(spacemit, true);

	dev_info(&pdev->dev, "onboard usb hub driver probed, hub configured\n");

	spacemit_hub_debugfs_init(spacemit);

	return 0;
err_rpm:
	pm_runtime_disable(dev);
	pm_runtime_put_sync(dev);
	pm_runtime_put_noidle(dev);
	return ret;
}

static int spacemit_hub_remove(struct platform_device *pdev)
{
	struct spacemit_hub_priv *spacemit = platform_get_drvdata(pdev);

	debugfs_remove(debugfs_lookup(dev_name(&pdev->dev), usb_debug_root));
	spacemit_hub_configure(spacemit, false);
	mutex_destroy(&spacemit->hub_mutex);
	dev_info(&pdev->dev, "onboard usb hub driver exit, disable hub\n");
	pm_runtime_disable(&pdev->dev);
	pm_runtime_put_sync(&pdev->dev);
	pm_runtime_put_noidle(&pdev->dev);
	return 0;
}

static const struct of_device_id spacemit_hub_dt_match[] = {
	{ .compatible = "spacemit,usb3-hub",},
	{},
};
MODULE_DEVICE_TABLE(of, spacemit_hub_dt_match);

#ifdef CONFIG_PM_SLEEP
static int spacemit_hub_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct spacemit_hub_priv *spacemit = platform_get_drvdata(pdev);
	mutex_lock(&spacemit->hub_mutex);
	if (!spacemit->suspend_power_on) {
		spacemit_hub_configure(spacemit, false);
		dev_info(dev, "turn off hub power supply\n");
	}
	mutex_unlock(&spacemit->hub_mutex);
	return 0;
}

static int spacemit_hub_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct spacemit_hub_priv *spacemit = platform_get_drvdata(pdev);
	mutex_lock(&spacemit->hub_mutex);
	if (!spacemit->suspend_power_on) {
		spacemit_hub_configure(spacemit, true);
		dev_info(dev, "resume hub power supply\n");
	}
	mutex_unlock(&spacemit->hub_mutex);
	return 0;
}

static const struct dev_pm_ops spacemit_onboard_hub_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(spacemit_hub_suspend, spacemit_hub_resume)
};
#define DEV_PM_OPS	(&spacemit_onboard_hub_pm_ops)
#else
#define DEV_PM_OPS	NULL
#endif /* CONFIG_PM_SLEEP */

static struct platform_driver spacemit_hub_driver = {
	.probe	= spacemit_hub_probe,
	.remove = spacemit_hub_remove,
	.driver = {
		.name   = "spacemit-usb3-hub",
		.owner  = THIS_MODULE,
		.of_match_table = of_match_ptr(spacemit_hub_dt_match),
		.pm = DEV_PM_OPS,
	},
};

module_platform_driver(spacemit_hub_driver);
MODULE_DESCRIPTION("Spacemit Onboard USB Hub driver");
MODULE_LICENSE("GPL v2");
