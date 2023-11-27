// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Regulator driver for Spacemit PMIC
 *
 * Copyright (c) 2023, SPACEMIT Co., Ltd
 *
 */

#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>
#include <linux/gpio/consumer.h>
#include <linux/mfd/spacemit/spacemit_pmic.h>

static const struct regulator_ops pmic_dcdc_ldo_ops = {
	.list_voltage		= regulator_list_voltage_linear,
	.map_voltage		= regulator_map_voltage_linear,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.set_voltage_time_sel	= regulator_set_voltage_time_sel,
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.is_enabled		= regulator_is_enabled_regmap,
};

static const struct regulator_ops pmic_switch_ops = {
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.is_enabled		= regulator_is_enabled_regmap,
};

SPM8821_BUCK_LINER_RANGE;SPM8821_LDO_LINER_RANGE;SPM8821_REGULATOR_DESC;

PM853_BUCK_LINER_RANGE1;PM853_BUCK_LINER_RANGE2;PM853_LDO_LINER_RANGE1;
PM853_LDO_LINER_RANGE2;PM853_LDO_LINER_RANGE3;PM853_LDO_LINER_RANGE4;
PM853_REGULATOR_DESC;

static int spacemit_regulator_probe(struct platform_device *pdev)
{
	struct regulator_config config = {};
	struct spacemit_pmic *pmic = dev_get_drvdata(pdev->dev.parent);
	struct i2c_client *client;
	const struct regulator_desc *regulators;
	struct regulator_dev *regulator_dev;
	int i, nregulators;

	switch (pmic->variant) {
	case SPM8821_ID:
		client = pmic->i2c;
		regulators = spm8821_reg;
		nregulators = ARRAY_SIZE(spm8821_reg);

		config.dev = &client->dev;
		config.regmap = pmic->regmap;
	       break;
	case PM853_ID:
		client = pmic->sub->power_page;
		regulators = pm853_reg;
		nregulators = ARRAY_SIZE(pm853_reg);

		config.dev = &client->dev;
		config.regmap = pmic->sub->power_regmap;
		break;
	default:
	       pr_err("unsupported Spacemit pmic ID: %d\n", pmic->variant);
	       return -EINVAL;
	}

	for (i = 0; i < nregulators; ++i) {
		regulator_dev = devm_regulator_register(&pdev->dev,
				regulators + i, &config);
		if (IS_ERR(regulator_dev)) {
			pr_err("failed to register %d regulator\n", i);
			return PTR_ERR(regulator_dev);
		}
	}

	return 0;
}

static struct platform_driver spacemit_regulator_driver = {
	.probe = spacemit_regulator_probe,
	.driver = {
		.name = "spacemit-regulator",
	},
};

module_platform_driver(spacemit_regulator_driver);

MODULE_DESCRIPTION("regulator drivers for the Spacemit series PMICs");

