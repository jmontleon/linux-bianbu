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

struct spacemit_hub_priv {
	struct gpio_desc *gpio_usb;
	struct gpio_desc *gpio_hub;
	struct gpio_desc *gpio_reset;
};

static int spacemit_hub_probe(struct platform_device *pdev)
{
	struct spacemit_hub_priv *spacemit;

	spacemit = devm_kzalloc(&pdev->dev, sizeof(*spacemit), GFP_KERNEL);
	if (!spacemit)
		return -ENOMEM;

	spacemit->gpio_usb = devm_gpiod_get(&pdev->dev, "usb", GPIOD_OUT_HIGH);
	if (IS_ERR_OR_NULL(spacemit->gpio_usb)) {
		dev_err(&pdev->dev, "can not find usb-gpio\n");
		return -ENODEV;
	}
	gpiod_set_value(spacemit->gpio_usb, 1);

	spacemit->gpio_hub = devm_gpiod_get(&pdev->dev, "hub", GPIOD_OUT_HIGH);
	if (IS_ERR_OR_NULL(spacemit->gpio_hub)) {
		dev_err(&pdev->dev, "can not find hub-gpio\n");
		return -ENODEV;
	}
	gpiod_set_value(spacemit->gpio_hub, 1);

	spacemit->gpio_reset= devm_gpiod_get(&pdev->dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR_OR_NULL(spacemit->gpio_reset)) {
		dev_err(&pdev->dev, "can not find reset-gpio\n");
		return -ENODEV;
	}
	gpiod_set_value(spacemit->gpio_reset, 1);

	dev_info(&pdev->dev, "onboard usb hub driver probe, hub configured\n");

	platform_set_drvdata(pdev, spacemit);

	return 0;
}

static int spacemit_hub_remove(struct platform_device *pdev)
{
	struct spacemit_hub_priv *spacemit = platform_get_drvdata(pdev);

	gpiod_set_value(spacemit->gpio_usb, 0);
	gpiod_set_value(spacemit->gpio_reset, 0);
	gpiod_set_value(spacemit->gpio_reset, 0);

	dev_info(&pdev->dev, "onboard usb hub driver exit, disable hub\n");
	return 0;
}

static const struct of_device_id spacemit_hub_dt_match[] = {
	{ .compatible = "spacemit,usb3-hub",},
	{},
};
MODULE_DEVICE_TABLE(of, spacemit_hub_dt_match);

static struct platform_driver spacemit_hub_driver = {
	.probe	= spacemit_hub_probe,
	.remove = spacemit_hub_remove,
	.driver = {
		.name   = "spacemit-usb3-hub",
		.owner  = THIS_MODULE,
		.of_match_table = of_match_ptr(spacemit_hub_dt_match),
	},
};

module_platform_driver(spacemit_hub_driver);
MODULE_DESCRIPTION("Spacemit Onboard USB Hub driver");
MODULE_LICENSE("GPL v2");
