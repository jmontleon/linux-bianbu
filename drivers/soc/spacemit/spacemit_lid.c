// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for simulating a mouse on GPIO lines.
 *
 * Copyright (C) 2007 Atmel Corporation
 * Copyright (C) 2017 Linus Walleij <linus.walleij@linaro.org>
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/pm_wakeirq.h>
#include <linux/gpio/consumer.h>
#include <linux/property.h>
#include <linux/of.h>

struct _hall {
	int irq;
	struct input_dev *input;
	struct gpio_desc *gpio;
};

static irqreturn_t hall_wakeup_detect(int irq, void *arg)
{
	unsigned char state = 0;
	struct _hall *hall = (struct _hall *)arg;

	state = gpiod_get_value(hall->gpio);
	input_report_switch(hall->input, SW_LID, !state);
 	input_sync(hall->input);

	return IRQ_HANDLED;
}

static int spacemit_lid_probe(struct platform_device *pdev)
{
	struct _hall *hall;
	struct input_dev *input;
	int error;

	hall = devm_kzalloc(&pdev->dev, sizeof(*hall), GFP_KERNEL);
	if (!hall)
		return -ENOMEM;

	hall->gpio = devm_gpiod_get(&pdev->dev, "lid", GPIOD_IN);
	if (IS_ERR_OR_NULL(hall->gpio)) {
		pr_err("get gpio error\n");
		return PTR_ERR(hall->gpio);
	}

	hall->irq = platform_get_irq(pdev, 0);
	if (hall->irq < 0)
		return -EINVAL;

	error = devm_request_any_context_irq(&pdev->dev, hall->irq,
			hall_wakeup_detect,
			IRQF_ONESHOT | IRQF_TRIGGER_NONE,
			"hall-detect", (void *)hall);
	if (error) {
		pr_err("request hall pinctrl dectect failed\n");
		return -EINVAL;
	}


	input = devm_input_allocate_device(&pdev->dev);
	if (!input)
		return -ENOMEM;

	input->name = pdev->name;
	input->id.bustype = BUS_HOST;
	input_set_capability(input, EV_SW, SW_LID);

	input_set_drvdata(input, hall);

	hall->input = input;

	error = input_register_device(input);
	if (error) {
		dev_err(&pdev->dev, "could not register input device\n");
		return error;
	}

	return 0;
}

static const struct of_device_id spacemit_lid_of_match[] = {
	{ .compatible = "spacemit,k1x-lid", },
	{ },
};
MODULE_DEVICE_TABLE(of, spacemit_lid_of_match);

static struct platform_driver spacemit_lid_device_driver = {
	.probe		= spacemit_lid_probe,
	.driver		= {
		.name	= "spacemit-lid",
		.of_match_table = spacemit_lid_of_match,
	}
};
module_platform_driver(spacemit_lid_device_driver);

MODULE_AUTHOR("Hans-Christian Egtvedt <egtvedt@samfundet.no>");
MODULE_DESCRIPTION("spacemit lid driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:spacemit-lid"); /* work with hotplug and coldplug */
