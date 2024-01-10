/*
 * spacemit-pwrseq.c -- power on/off pwrseq part of SoC
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
#include <linux/of_gpio.h>
#include <linux/clk.h>
#include <linux/of_platform.h>
#include <linux/regulator/consumer.h>
#include <linux/platform_device.h>

struct spacemit_pwrseq {
	struct device		*dev;
	bool clk_enabled;
	u32 power_on_delay_ms;

	struct clk *ext_clk;
	struct gpio_descs *pwr_gpios;
	struct regulator *vdd_supply;
	struct regulator *io_supply;
	int	vdd_voltage;
	int io_voltage;
};

static void spacemit_set_gpios_value(struct spacemit_pwrseq *pwrseq,
						int value)
{
	struct gpio_descs *pwr_gpios = pwrseq->pwr_gpios;

	if (!IS_ERR(pwr_gpios)) {
		unsigned long *values;
		int nvalues = pwr_gpios->ndescs;

		values = bitmap_alloc(nvalues, GFP_KERNEL);
		if (!values)
			return;

		if (value)
			bitmap_fill(values, nvalues);
		else
			bitmap_zero(values, nvalues);

		gpiod_set_array_value_cansleep(nvalues, pwr_gpios->desc,
					       pwr_gpios->info, values);

		bitmap_free(values);
	}
}

static int spacemit_regulator_set_voltage_if_supported(struct regulator *regulator,
						  int min_uV, int target_uV,
						  int max_uV)
{
	int current_uV;

	/*
	 * Check if supported first to avoid errors since we may try several
	 * signal levels during power up and don't want to show errors.
	 */
	if (!regulator_is_supported_voltage(regulator, min_uV, max_uV))
		return -EINVAL;

	/*
	 * The voltage is already set, no need to switch.
	 * Return 1 to indicate that no switch happened.
	 */
	current_uV = regulator_get_voltage(regulator);
	if (current_uV == target_uV)
		return 1;

	return regulator_set_voltage_triplet(regulator, min_uV, target_uV,
					     max_uV);
}

static void spacemit_regulator_on(struct spacemit_pwrseq *pwrseq,
						struct regulator *regulator, bool on_off)
{
	struct device *dev = pwrseq->dev;
	int ret;

	if(on_off){
		if(regulator == pwrseq->io_supply){
			ret = spacemit_regulator_set_voltage_if_supported(regulator,
						1700000, pwrseq->io_voltage, 1950000);
		}else{
			ret = spacemit_regulator_set_voltage_if_supported(regulator,
						2700000, pwrseq->vdd_voltage, 3600000);
		}

		if (ret < 0) {
			dev_err(dev, "set vdd_supply voltage failed!\n");
			return;
		}

		ret = regulator_enable(regulator);
		if (ret < 0) {
			dev_err(dev, "enable vdd_supply failed\n");
			return;
		}

		ret = regulator_get_voltage(regulator);
		if (ret < 0) {
			dev_err(dev, "get vdd_supply voltage failed\n");
			return;
		}

		dev_info(dev, "check vdd_supply voltage: %d\n", ret);
	}else{
		regulator_disable(regulator);
	}
}

static void spacemit_pre_power_on(struct spacemit_pwrseq *pwrseq,
						bool on_off)
{
	if(!IS_ERR(pwrseq->vdd_supply))
		spacemit_regulator_on(pwrseq, pwrseq->vdd_supply, on_off);

	if(!IS_ERR(pwrseq->io_supply))
		spacemit_regulator_on(pwrseq, pwrseq->io_supply, on_off);
}

static void spacemit_post_power_on(struct spacemit_pwrseq *pwrseq,
						bool on_off)
{
	if (!IS_ERR(pwrseq->ext_clk)) {
		if(on_off && !pwrseq->clk_enabled){
			clk_prepare_enable(pwrseq->ext_clk);
			pwrseq->clk_enabled = true;
		}

		if(!on_off && pwrseq->clk_enabled){
			clk_disable_unprepare(pwrseq->ext_clk);
			pwrseq->clk_enabled = false;
		}
	}
}

void spacemit_power_on(struct spacemit_pwrseq *pwrseq,
						bool on_off)
{
	if(on_off){
		spacemit_pre_power_on(pwrseq, on_off);
		spacemit_set_gpios_value(pwrseq, on_off);
		if (pwrseq->power_on_delay_ms)
			msleep(pwrseq->power_on_delay_ms);
		spacemit_post_power_on(pwrseq, on_off);
	}else{
		spacemit_post_power_on(pwrseq, on_off);
		spacemit_set_gpios_value(pwrseq, on_off);
		spacemit_pre_power_on(pwrseq, on_off);
	}
}

static int spacemit_pwrseq_probe(struct platform_device *pdev)
{
	struct device		*dev = &pdev->dev;
	struct device_node	*node = dev->of_node;
	struct spacemit_pwrseq *pwrseq;
	int ret;

	pwrseq = devm_kzalloc(dev, sizeof(*pwrseq), GFP_KERNEL);
	if (!pwrseq)
		return -ENOMEM;

	pwrseq->dev = dev;
	platform_set_drvdata(pdev, pwrseq);

	pwrseq->vdd_supply = devm_regulator_get_optional(dev, "vdd");
	if (IS_ERR(pwrseq->vdd_supply)) {
		if (PTR_ERR(pwrseq->vdd_supply) == -EPROBE_DEFER)
			return -EPROBE_DEFER;
		dev_dbg(dev, "No vdd regulator found\n");
	}else{
		if (!device_property_read_u32(dev, "vdd_voltage", &pwrseq->vdd_voltage)) {
			pwrseq->vdd_voltage = 3300000;
			dev_dbg(dev, "failed get vdd voltage,use default value (%u)\n", pwrseq->vdd_voltage);
		}
	}

	pwrseq->io_supply = devm_regulator_get_optional(dev, "io");
	if (IS_ERR(pwrseq->io_supply)) {
		if (PTR_ERR(pwrseq->io_supply) == -EPROBE_DEFER)
			return -EPROBE_DEFER;
		dev_dbg(dev, "No io regulator found\n");
	}else{
		if (!device_property_read_u32(dev, "io_voltage", &pwrseq->io_voltage)) {
			pwrseq->io_voltage = 1800000;
			dev_dbg(dev, "failed get io voltage,use default value (%u)\n", pwrseq->io_voltage);
		}
	}

	pwrseq->pwr_gpios = devm_gpiod_get_array(dev, "pwr",
							GPIOD_OUT_LOW);
	if (IS_ERR(pwrseq->pwr_gpios) &&
		PTR_ERR(pwrseq->pwr_gpios) != -ENOENT &&
		PTR_ERR(pwrseq->pwr_gpios) != -ENOSYS) {
		return PTR_ERR(pwrseq->pwr_gpios);
	}

	pwrseq->ext_clk = devm_clk_get(dev, "clock");
	if (IS_ERR(pwrseq->ext_clk) && PTR_ERR(pwrseq->ext_clk) != -ENOENT){
		dev_dbg(dev, "failed get ext clock\n");
		return PTR_ERR(pwrseq->ext_clk);
	}

	device_property_read_u32(dev, "power-on-delay-ms",
				 &pwrseq->power_on_delay_ms);

	if (node) {
		ret = of_platform_populate(node, NULL, NULL, dev);
		if (ret) {
			dev_err(dev, "failed to add sub pwrseq\n");
			return ret;
		}
	} else {
		dev_err(dev, "no device node, failed to add sub pwrseq\n");
		ret = -ENODEV;
		return ret;
	}

	spacemit_power_on(pwrseq, 1);
	return 0;
}

static int spacemit_pwrseq_remove(struct platform_device *pdev)
{
	struct spacemit_pwrseq	*pwrseq = platform_get_drvdata(pdev);

	of_platform_depopulate(&pdev->dev);

	if (pwrseq->ext_clk >= 0)
		clk_disable_unprepare(pwrseq->ext_clk);

	return 0;
}

static const struct of_device_id spacemit_pwrseq_ids[] = {
	{ .compatible = "spacemit,rf-pwrseq" },
	{ /* Sentinel */ }
};

#ifdef CONFIG_PM_SLEEP
static int spacemit_pwrseq_suspend(struct device *dev)
{
	return 0;
}

static int spacemit_pwrseq_resume(struct device *dev)
{
	return 0;
}

static const struct dev_pm_ops spacemit_pwrseq_dev_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(spacemit_pwrseq_suspend, spacemit_pwrseq_resume)
};

#define DEV_PM_OPS	(&spacemit_pwrseq_dev_pm_ops)
#else
#define DEV_PM_OPS	NULL
#endif /* CONFIG_PM_SLEEP */

static struct platform_driver spacemit_pwrseq_driver = {
	.probe		= spacemit_pwrseq_probe,
	.remove	= spacemit_pwrseq_remove,
	.driver	= {
		.owner	= THIS_MODULE,
		.name	= "spacemit-rf-pwrseq",
		.of_match_table	= spacemit_pwrseq_ids,
	},
};

module_platform_driver(spacemit_pwrseq_driver);

MODULE_DESCRIPTION("spacemit rf pwrseq driver");
MODULE_LICENSE("GPL");
