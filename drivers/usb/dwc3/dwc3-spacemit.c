// SPDX-License-Identifier: GPL-2.0
/*
 * dwc3-spacemit.c - Spacemit DWC3 Specific Glue layer
 *
 * Copyright (c) 2023 Spacemit Co., Ltd.
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/usb/phy.h>
#include <linux/phy/phy.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/reset.h>
#include <linux/of_address.h>
#include <linux/pm_wakeirq.h>

#define DWC3_LFPS_WAKE_STATUS		(1 << 29)
#define DWC3_CDWS_WAKE_STATUS		(1 << 28)
#define DWC3_ID_WAKE_STATUS			(1 << 27)
#define DWC3_VBUS_WAKE_STATUS		(1 << 26)
#define DWC3_LINS1_WAKE_STATUS		(1 << 25)
#define DWC3_LINS0_WAKE_STATUS		(1 << 24)

#define DWC3_CDWS_WAKE_CLEAR		(1 << 20)
#define DWC3_ID_WAKE_CLEAR		    (1 << 19)
#define DWC3_VBUS_WAKE_CLEAR		(1 << 18)
#define DWC3_LINS1_WAKE_CLEAR		(1 << 17)
#define DWC3_LINS0_WAKE_CLEAR		(1 << 16)
#define DWC3_LFPS_WAKE_CLEAR		(1 << 14)

#define DWC3_WAKEUP_INT_MASK		(1 << 15)
#define DWC3_LFPS_WAKE_MASK			(1 << 13)
#define DWC3_CDWS_WAKE_MASK			(1 << 12)
#define DWC3_ID_WAKE_MASK			(1 << 11)
#define DWC3_VBUS_WAKE_MASK			(1 << 10)
#define DWC3_LINS1_WAKE_MASK		(1 <<  9)
#define DWC3_LINS0_WAKE_MASK		(1 <<  8)

#define DWC3_SPACEMIT_MAX_CLOCKS	4


struct dwc3_spacemit_driverdata {
	const char		*clk_names[DWC3_SPACEMIT_MAX_CLOCKS];
	int			num_clks;
	int			suspend_clk_idx;
	bool		need_notify_disconnect;
};

struct dwc3_spacemit {
	struct device		*dev;
	struct reset_control	*resets;

	const char		**clk_names;
	struct clk		*clks[DWC3_SPACEMIT_MAX_CLOCKS];
	int			num_clks;
	int			suspend_clk_idx;
	bool		reset_on_resume;

	struct usb_phy		*usb2_phy;
	struct usb_phy		*usb3_phy;
	struct phy		*usb2_generic_phy;
	struct phy		*usb3_generic_phy;

	bool 		need_notify_disconnect;
	int	irq;
	void __iomem *wakeup_reg;
};

static void dwc3_spacemit_enable_wakeup_irqs(struct dwc3_spacemit *spacemit)
{
	u32 reg;
	reg = readl(spacemit->wakeup_reg);
	reg |= (DWC3_LFPS_WAKE_MASK | DWC3_LINS0_WAKE_MASK | DWC3_LINS1_WAKE_MASK);
	writel(reg, spacemit->wakeup_reg);
}

static void dwc3_spacemit_disable_wakeup_irqs(struct dwc3_spacemit *spacemit)
{
	u32 reg;
	reg = readl(spacemit->wakeup_reg);
	reg &= ~(DWC3_LFPS_WAKE_MASK | DWC3_LINS0_WAKE_MASK | DWC3_LINS1_WAKE_MASK);
	writel(reg, spacemit->wakeup_reg);
}

static void dwc3_spacemit_clear_wakeup_irqs(struct dwc3_spacemit *spacemit)
{
	u32 reg;
	reg = readl(spacemit->wakeup_reg);
	reg |= (DWC3_LFPS_WAKE_CLEAR | DWC3_LINS0_WAKE_CLEAR | DWC3_LINS1_WAKE_CLEAR);
	writel(reg, spacemit->wakeup_reg);
}

static irqreturn_t dwc3_spacemit_wakeup_interrupt(int irq, void *_spacemit)
{
	struct dwc3_spacemit	*spacemit = _spacemit;
	u32 reg;
	reg = readl(spacemit->wakeup_reg);
	dev_dbg(spacemit->dev, "wakeup_reg: 0x%x\n", reg);

	dwc3_spacemit_disable_wakeup_irqs(spacemit);
	dwc3_spacemit_clear_wakeup_irqs(spacemit);

	return IRQ_HANDLED;
}

void dwc3_spacemit_clear_disconnect(struct device *dev)
{
	struct platform_device *pdev;
	struct dwc3_spacemit *spacemit;
	dev_dbg(dev, "%s: clear disconnect\n", __func__);
	if (!dev)
		return;
	pdev = to_platform_device(dev);
	if (IS_ERR_OR_NULL(pdev))
		return;
	spacemit = platform_get_drvdata(pdev);
	if(!spacemit->need_notify_disconnect)
		return;
	usb_phy_notify_disconnect(spacemit->usb2_phy, USB_SPEED_HIGH);
}

static int dwc3_spacemit_get_phy(struct dwc3_spacemit *spacemit)
{
	struct device		*dev = spacemit->dev;
	int ret;

	spacemit->usb2_phy = devm_usb_get_phy_by_phandle(dev, "usb-phy", 0);
	spacemit->usb3_phy = devm_usb_get_phy_by_phandle(dev, "usb-phy", 1);
	if (IS_ERR(spacemit->usb2_phy)) {
		ret = PTR_ERR(spacemit->usb2_phy);
		if (ret == -ENXIO || ret == -ENODEV)
			spacemit->usb2_phy = NULL;
		else
			return dev_err_probe(dev, ret, "no usb2 phy configured\n");
	}

	if (IS_ERR(spacemit->usb3_phy)) {
		ret = PTR_ERR(spacemit->usb3_phy);
		if (ret == -ENXIO || ret == -ENODEV)
			spacemit->usb3_phy = NULL;
		else
			return dev_err_probe(dev, ret, "no usb3 phy configured\n");
	}

	spacemit->usb2_generic_phy = devm_phy_get(dev, "usb2-phy");
	if (IS_ERR(spacemit->usb2_generic_phy)) {
		ret = PTR_ERR(spacemit->usb2_generic_phy);
		if (ret == -ENOSYS || ret == -ENODEV)
			spacemit->usb2_generic_phy = NULL;
		else
			return dev_err_probe(dev, ret, "no usb2 phy configured\n");
	}

	spacemit->usb3_generic_phy = devm_phy_get(dev, "usb3-phy");
	if (IS_ERR(spacemit->usb3_generic_phy)) {
		ret = PTR_ERR(spacemit->usb3_generic_phy);
		if (ret == -ENOSYS || ret == -ENODEV)
			spacemit->usb3_generic_phy = NULL;
		else
			return dev_err_probe(dev, ret, "no usb3 phy configured\n");
	}

	return 0;
}

static int dwc3_spacemit_phy_setup(struct dwc3_spacemit *spacemit, bool enable)
{
	if (enable) {
		usb_phy_init(spacemit->usb2_phy);
		usb_phy_init(spacemit->usb3_phy);
		phy_init(spacemit->usb2_generic_phy);
		phy_init(spacemit->usb3_generic_phy);
	} else {
		usb_phy_shutdown(spacemit->usb2_phy);
		usb_phy_shutdown(spacemit->usb3_phy);
		phy_exit(spacemit->usb2_generic_phy);
		phy_exit(spacemit->usb3_generic_phy);
	}
	return 0;
}

static int dwc3_spacemit_init(struct dwc3_spacemit *data)
{
	struct device *dev = data->dev;
	int	ret = 0, i;

	for (i = 0; i < data->num_clks; i++) {
		ret = clk_prepare_enable(data->clks[i]);
		if (ret) {
			while (i-- > 0)
				clk_disable_unprepare(data->clks[i]);
			return ret;
		}
	}

	if (data->suspend_clk_idx >= 0)
		clk_prepare_enable(data->clks[data->suspend_clk_idx]);

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

	dwc3_spacemit_phy_setup(data, true);

	return 0;
}

static int dwc3_spacemit_exit(struct dwc3_spacemit *data)
{
	struct device *dev = data->dev;
	int	ret = 0, i;

	dwc3_spacemit_phy_setup(data, false);

	ret = reset_control_assert(data->resets);
	if (ret) {
		dev_err(dev, "failed to assert resets, err=%d\n", ret);
		return ret;
	}

	if (data->suspend_clk_idx >= 0)
		clk_disable_unprepare(data->clks[data->suspend_clk_idx]);

	for (i = data->num_clks - 1; i >= 0; i--)
		clk_disable_unprepare(data->clks[i]);

	return 0;
}

static int dwc3_spacemit_probe(struct platform_device *pdev)
{
	struct dwc3_spacemit	*spacemit;
	struct device		*dev = &pdev->dev;
	struct device_node	*node = dev->of_node;
	const struct dwc3_spacemit_driverdata *driver_data;
	struct resource		*res;
	bool wakeup_source;
	int			i, ret;

	spacemit = devm_kzalloc(dev, sizeof(*spacemit), GFP_KERNEL);
	if (!spacemit)
		return -ENOMEM;

	driver_data = of_device_get_match_data(dev);
	spacemit->dev = dev;
	spacemit->num_clks = driver_data->num_clks;
	spacemit->clk_names = (const char **)driver_data->clk_names;
	spacemit->suspend_clk_idx = driver_data->suspend_clk_idx;
	spacemit->need_notify_disconnect = driver_data->need_notify_disconnect;
	spacemit->reset_on_resume = device_property_read_bool(&pdev->dev, "reset-on-resume");

	platform_set_drvdata(pdev, spacemit);

	pm_runtime_enable(dev);
	pm_runtime_get_noresume(dev);
	pm_runtime_get_sync(dev);

	spacemit->irq = platform_get_irq(pdev, 0);
	if (spacemit->irq < 0) {
		dev_err(dev, "missing IRQ resource\n");
		return -EINVAL;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(dev, "missing wakeup base resource\n");
		return -ENODEV;
	}

	spacemit->wakeup_reg = devm_ioremap(&pdev->dev, res->start, resource_size(res));
	if (!spacemit->wakeup_reg) {
		dev_err(dev, " wakeup reg ioremap failed\n");
		return -ENODEV;
	}

	for (i = 0; i < spacemit->num_clks; i++) {
		spacemit->clks[i] = devm_clk_get(dev, spacemit->clk_names[i]);
		if (IS_ERR(spacemit->clks[i])) {
			dev_err(dev, "failed to get clock: %s\n",
				spacemit->clk_names[i]);
			return PTR_ERR(spacemit->clks[i]);
		}
	}

	spacemit->resets = devm_reset_control_array_get_optional_exclusive(dev);
	if (IS_ERR(spacemit->resets)) {
		ret = PTR_ERR(spacemit->resets);
		dev_err(dev, "failed to get resets, err=%d\n", ret);
		return ret;
	}

	ret = dwc3_spacemit_get_phy(spacemit);
	if (ret)
		return ret;

	ret = dwc3_spacemit_init(spacemit);
	if (ret) {
		dev_err(dev, "failed to init spacemit\n");
		return ret;
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

	ret = devm_request_irq(dev, spacemit->irq, dwc3_spacemit_wakeup_interrupt, IRQF_NO_SUSPEND,
			"dwc3-usb-wakeup", spacemit);
	if (ret) {
		dev_err(dev, "failed to request IRQ #%d --> %d\n",
				spacemit->irq, ret);
		goto irq_err;
	}

	wakeup_source = of_property_read_bool(dev->of_node, "wakeup-source");
	if (wakeup_source) {
		device_init_wakeup(dev, true);
		dev_pm_set_wake_irq(dev, spacemit->irq);
	}
	return 0;

irq_err:
	of_platform_depopulate(&pdev->dev);
populate_err:
	dwc3_spacemit_exit(spacemit);
	pm_runtime_disable(dev);
	pm_runtime_put_sync(dev);
	pm_runtime_put_noidle(dev);
	return ret;
}

static int dwc3_spacemit_remove(struct platform_device *pdev)
{
	struct dwc3_spacemit	*spacemit = platform_get_drvdata(pdev);
	bool do_wakeup = device_may_wakeup(&pdev->dev);

	if (do_wakeup) {
		dwc3_spacemit_disable_wakeup_irqs(spacemit);
		dev_pm_clear_wake_irq(spacemit->dev);
		device_init_wakeup(spacemit->dev, false);
	}
	of_platform_depopulate(&pdev->dev);
	dwc3_spacemit_exit(spacemit);
	pm_runtime_disable(&pdev->dev);
	pm_runtime_put_sync(&pdev->dev);
	pm_runtime_put_noidle(&pdev->dev);

	return 0;
}

static const struct dwc3_spacemit_driverdata spacemit_k1pro_drvdata = {
	.clk_names = { "usbdrd30" },
	.num_clks = 0,
	.suspend_clk_idx = -1,
	.need_notify_disconnect = false,
};

static const struct dwc3_spacemit_driverdata spacemit_k1x_drvdata = {
	.clk_names = { "usbdrd30" },
	.num_clks = 1,
	.suspend_clk_idx = -1,
	.need_notify_disconnect = true,
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
	bool do_wakeup = device_may_wakeup(dev);
	int i, ret;

	dwc3_spacemit_phy_setup(spacemit, false);
	if (spacemit->reset_on_resume){
		ret = reset_control_assert(spacemit->resets);
		if (ret)
			return ret;
		dev_info(spacemit->dev, "Will reset controller and phy on resume\n");
	}
	for (i = spacemit->num_clks - 1; i >= 0; i--)
		clk_disable_unprepare(spacemit->clks[i]);

	if (do_wakeup) {
		dwc3_spacemit_clear_wakeup_irqs(spacemit);
		dwc3_spacemit_enable_wakeup_irqs(spacemit);
	}
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
	if (spacemit->reset_on_resume){
		dev_info(spacemit->dev, "Resetting controller and phy\n");
		ret = reset_control_deassert(spacemit->resets);
		if (ret)
			return ret;
	}
	dwc3_spacemit_phy_setup(spacemit, true);

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

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("DesignWare USB3 Spacemit Glue Layer");
