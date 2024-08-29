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
#include <linux/gpio/consumer.h>
#include <linux/property.h>
#include <linux/of.h>

/**
 * struct gpio_lid
 *
 * This struct must be added to the platform_device in the board code.
 * It is used by the gpio_lid driver to setup GPIO lines and to
 * calculate mouse movement.
 */
struct gpio_lid {
	u32 scan_ms;
	struct gpio_desc *blid;
	int old_state;
};

/*
 * Timer function which is run every scan_ms ms when the device is opened.
 * The dev input variable is set to the input_dev pointer.
 */
static void spacemit_lid_scan(struct input_dev *input)
{
	struct gpio_lid *gpio = input_get_drvdata(input);
	int state = 0;

	if (gpio->blid)
		state = gpiod_get_value(gpio->blid);

	if (gpio->old_state != state) {
		dev_warn(&input->dev, "spacemit_lid: state = %d\n", state);
		gpio->old_state = state;
 		input_report_switch(input, SW_LID, !state);
 		input_sync(input);
	}
}

static int spacemit_lid_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct gpio_lid *glid;
	struct input_dev *input;
	int error;

	glid = devm_kzalloc(dev, sizeof(*glid), GFP_KERNEL);
	if (!glid)
		return -ENOMEM;

	/* Assign some default scanning time */
	error = device_property_read_u32(dev, "scan-interval-ms", &glid->scan_ms);
	if (error || glid->scan_ms == 0) {
		dev_warn(dev, "invalid scan time, set to 1000 ms\n");
		glid->scan_ms = 1000;
	}

	glid->blid = devm_gpiod_get(dev, "lid", GPIOD_IN);
	if (IS_ERR(glid->blid))
		return PTR_ERR(glid->blid);

	glid->old_state = 0;

	input = devm_input_allocate_device(dev);
	if (!input)
		return -ENOMEM;

	input->name = pdev->name;
	input->id.bustype = BUS_HOST;

	input_set_drvdata(input, glid);

	if (glid->blid)
		input_set_capability(input, EV_SW, SW_LID);

	error = input_setup_polling(input, spacemit_lid_scan);
	if (error)
		return error;

	input_set_poll_interval(input, glid->scan_ms);

	error = input_register_device(input);
	if (error) {
		dev_err(dev, "could not register input device\n");
		return error;
	}

	dev_dbg(dev, "%d ms scan time, buttons: %s\n",
		glid->scan_ms,
		glid->blid ? "" : "lid");

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
