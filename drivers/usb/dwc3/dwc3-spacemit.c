// SPDX-License-Identifier: GPL-2.0
/*
 * dwc3-spacemit.c - Spacemit DWC3 Specific Glue layer
 *
 * Copyright (c) 2023 Spacemit Co., Ltd.
 *
 * Author: Wilson <long.wan@spacemit.com>
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/reset.h>
#include <linux/of_address.h>

#define DWC3_SPACEMIT_MAX_CLOCKS	4

struct dwc3_spacemit_driverdata {
	const char		*clk_names[DWC3_SPACEMIT_MAX_CLOCKS];
	int			num_clks;
	int			suspend_clk_idx;
};

struct dwc3_spacemit {
	struct device		*dev;
	struct reset_control	*resets;

	const char		**clk_names;
	struct clk		*clks[DWC3_SPACEMIT_MAX_CLOCKS];
	int			num_clks;
	int			suspend_clk_idx;
};

static int dwc3_spacemit_init(struct dwc3_spacemit *data)
{
	struct device *dev = data->dev;
	int	ret = 0;

	data->resets = devm_reset_control_array_get_optional_exclusive(dev);
	if (IS_ERR(data->resets)) {
		ret = PTR_ERR(data->resets);
		dev_err(dev, "failed to get resets, err=%d\n", ret);
		return ret;
	}

	ret = reset_control_assert(data->resets);
	if (ret) {
		dev_err(dev, "failed to assert resets, err=%d\n", ret);
		return ret;
	}

	ret = reset_control_deassert(data->resets);
	if (ret) {
		dev_err(dev, "failed to deassert resets, err=%d\n", ret);
		return ret;
	}

	return 0;
}

static int dwc3_spacemit_probe(struct platform_device *pdev)
{
	struct dwc3_spacemit	*spacemit;
	struct device		*dev = &pdev->dev;
	struct device_node	*node = dev->of_node;
	const struct dwc3_spacemit_driverdata *driver_data;
	int			i, ret;

	spacemit = devm_kzalloc(dev, sizeof(*spacemit), GFP_KERNEL);
	if (!spacemit)
		return -ENOMEM;

	driver_data = of_device_get_match_data(dev);
	spacemit->dev = dev;
	spacemit->num_clks = driver_data->num_clks;
	spacemit->clk_names = (const char **)driver_data->clk_names;
	spacemit->suspend_clk_idx = driver_data->suspend_clk_idx;

	platform_set_drvdata(pdev, spacemit);

	for (i = 0; i < spacemit->num_clks; i++) {
		spacemit->clks[i] = devm_clk_get(dev, spacemit->clk_names[i]);
		if (IS_ERR(spacemit->clks[i])) {
			dev_err(dev, "failed to get clock: %s\n",
				spacemit->clk_names[i]);
			return PTR_ERR(spacemit->clks[i]);
		}
	}

	for (i = 0; i < spacemit->num_clks; i++) {
		ret = clk_prepare_enable(spacemit->clks[i]);
		if (ret) {
			while (i-- > 0)
				clk_disable_unprepare(spacemit->clks[i]);
			return ret;
		}
	}

	if (spacemit->suspend_clk_idx >= 0)
		clk_prepare_enable(spacemit->clks[spacemit->suspend_clk_idx]);

	ret = dwc3_spacemit_init(spacemit);
	if (ret) {
		dev_err(dev, "failed to init spacemit\n");
		goto populate_err;
	}

	if (node) {
		ret = of_platform_populate(node, NULL, NULL, dev);
		if (ret) {
			dev_err(dev, "failed to add dwc3 core\n");
			goto populate_err;
		}
	} else {
		dev_err(dev, "no device node, failed to add dwc3 core\n");
		ret = -ENODEV;
		goto populate_err;
	}

	return 0;

populate_err:
	for (i = spacemit->num_clks - 1; i >= 0; i--)
		clk_disable_unprepare(spacemit->clks[i]);

	if (spacemit->suspend_clk_idx >= 0)
		clk_disable_unprepare(spacemit->clks[spacemit->suspend_clk_idx]);

	return ret;
}

static int dwc3_spacemit_remove(struct platform_device *pdev)
{
	struct dwc3_spacemit	*spacemit = platform_get_drvdata(pdev);
	int i;

	of_platform_depopulate(&pdev->dev);

	for (i = spacemit->num_clks - 1; i >= 0; i--)
		clk_disable_unprepare(spacemit->clks[i]);

	if (spacemit->suspend_clk_idx >= 0)
		clk_disable_unprepare(spacemit->clks[spacemit->suspend_clk_idx]);

	return 0;
}

static const struct dwc3_spacemit_driverdata spacemit_k1pro_drvdata = {
	.clk_names = { "usbdrd30" },
	.num_clks = 0,
	.suspend_clk_idx = -1,
};

static const struct dwc3_spacemit_driverdata spacemit_k1x_drvdata = {
	.clk_names = { "usbdrd30" },
	.num_clks = 1,
	.suspend_clk_idx = -1,
};

static const struct of_device_id spacemit_dwc3_match[] = {
	{
		.compatible = "spacemit,k1-pro-dwc3",
		.data = &spacemit_k1pro_drvdata,
	},
	{
		.compatible = "spacemit,k1-x-dwc3",
		.data = &spacemit_k1x_drvdata,
	},
	{ /* Sentinel */ }
};
MODULE_DEVICE_TABLE(of, spacemit_dwc3_match);

#ifdef CONFIG_PM_SLEEP
static int dwc3_spacemit_suspend(struct device *dev)
{
	struct dwc3_spacemit *spacemit = dev_get_drvdata(dev);
	int i;

	for (i = spacemit->num_clks - 1; i >= 0; i--)
		clk_disable_unprepare(spacemit->clks[i]);

	return 0;
}

static int dwc3_spacemit_resume(struct device *dev)
{
	struct dwc3_spacemit *spacemit = dev_get_drvdata(dev);
	int i, ret;

	for (i = 0; i < spacemit->num_clks; i++) {
		ret = clk_prepare_enable(spacemit->clks[i]);
		if (ret) {
			while (i-- > 0)
				clk_disable_unprepare(spacemit->clks[i]);
			return ret;
		}
	}

	return 0;
}

static const struct dev_pm_ops dwc3_spacemit_dev_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(dwc3_spacemit_suspend, dwc3_spacemit_resume)
};

#define DEV_PM_OPS	(&dwc3_spacemit_dev_pm_ops)
#else
#define DEV_PM_OPS	NULL
#endif /* CONFIG_PM_SLEEP */

static struct platform_driver dwc3_spacemit_driver = {
	.probe		= dwc3_spacemit_probe,
	.remove		= dwc3_spacemit_remove,
	.driver		= {
		.name	= "spacemit-dwc3",
		.of_match_table = spacemit_dwc3_match,
		.pm	= DEV_PM_OPS,
	},
};

module_platform_driver(dwc3_spacemit_driver);

MODULE_AUTHOR("Wilson <long.wan@spacemit.com>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("DesignWare USB3 Spacemit Glue Layer");
