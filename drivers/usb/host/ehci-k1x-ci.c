// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * EHCI Driver for Spacemit k1x SoCs
 *
 * Copyright (c) 2023 Spacemit Inc.
 */

#include "linux/reset.h"
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/err.h>
#include <linux/usb/otg.h>
#include <linux/usb.h>
#include <linux/usb/ch9.h>
#include <linux/usb/otg.h>
#include <linux/usb/gadget.h>
#include <linux/usb/hcd.h>
#include <linux/of_address.h>
#include <linux/platform_data/k1x_ci_usb.h>
#include <dt-bindings/usb/k1x_ci_usb.h>
#include <linux/regulator/consumer.h>

#define CAPLENGTH_MASK         (0xff)

#define PMU_SD_ROT_WAKE_CLR	0x7C
#define PMU_SD_ROT_WAKE_CLR_VBUS_DRV	(0x1 << 21)

struct ehci_hcd_mv {
	struct usb_hcd *hcd;
	struct usb_phy *phy;

	/* Which mode does this ehci running OTG/Host ? */
	int mode;

	void __iomem *cap_regs;
	void __iomem *op_regs;
	void __iomem *apmu_base;

	struct usb_phy *otg;

	struct mv_usb_platform_data *pdata;

	struct clk *clk;
	struct reset_control *reset;
	struct regulator *vbus_otg;

};

static int ehci_otg_enable(struct device *dev, struct ehci_hcd_mv *ehci_mv, bool enable)
{
	uint32_t temp;

	temp = readl(ehci_mv->apmu_base + PMU_SD_ROT_WAKE_CLR);
	if (enable)
		writel(PMU_SD_ROT_WAKE_CLR_VBUS_DRV | temp, ehci_mv->apmu_base + PMU_SD_ROT_WAKE_CLR);
	else
		writel(temp & ~PMU_SD_ROT_WAKE_CLR_VBUS_DRV , ehci_mv->apmu_base + PMU_SD_ROT_WAKE_CLR);

	return 0;
}

static void ehci_clock_enable(struct ehci_hcd_mv *ehci_mv)
{
	clk_enable(ehci_mv->clk);
}

static void ehci_clock_disable(struct ehci_hcd_mv *ehci_mv)
{
	clk_disable(ehci_mv->clk);
}

static void mv_ehci_disable(struct ehci_hcd_mv *ehci_mv)
{
	usb_phy_shutdown(ehci_mv->phy);
	reset_control_assert(ehci_mv->reset);
	ehci_clock_disable(ehci_mv);
}

static int mv_ehci_reset(struct usb_hcd *hcd)
{
	struct device *dev = hcd->self.controller;
	struct ehci_hcd_mv *ehci_mv = dev_get_drvdata(dev);
	int retval;

	if (ehci_mv == NULL) {
		dev_err(dev, "Can not find private ehci data\n");
		return -ENODEV;
	}

	hcd->has_tt = 1;

	retval = ehci_setup(hcd);
	if (retval)
		dev_err(dev, "ehci_setup failed %d\n", retval);

	return retval;
}

static const struct hc_driver mv_ehci_hc_driver = {
	.description = hcd_name,
	.product_desc = "Spacemit EHCI",
	.hcd_priv_size = sizeof(struct ehci_hcd),

	/*
	 * generic hardware linkage
	 */
	.irq = ehci_irq,
	.flags = HCD_MEMORY | HCD_USB2 | HCD_BH | HCD_DMA,

	/*
	 * basic lifecycle operations
	 */
	.reset = mv_ehci_reset,
	.start = ehci_run,
	.stop = ehci_stop,
	.shutdown = ehci_shutdown,

	/*
	 * managing i/o requests and associated device resources
	 */
	.urb_enqueue = ehci_urb_enqueue,
	.urb_dequeue = ehci_urb_dequeue,
	.endpoint_disable = ehci_endpoint_disable,
	.endpoint_reset = ehci_endpoint_reset,
	.clear_tt_buffer_complete = ehci_clear_tt_buffer_complete,

	/*
	 * scheduling support
	 */
	.get_frame_number = ehci_get_frame,

	/*
	 * root hub support
	 */
	.hub_status_data =	ehci_hub_status_data,
	.hub_control =		ehci_hub_control,
	.bus_suspend =		ehci_bus_suspend,
	.bus_resume =		ehci_bus_resume,
	.relinquish_port =	ehci_relinquish_port,
	.port_handed_over =	ehci_port_handed_over,
	.get_resuming_ports =	ehci_get_resuming_ports,

	/*
	 * device support
	 */
	.free_dev =		ehci_remove_device,
};

static int mv_ehci_dt_parse(struct platform_device *pdev,
			struct mv_usb_platform_data *pdata)
{
	struct device_node *np = pdev->dev.of_node;

	if (of_property_read_string(np,
			"spacemit,ehci-name", &((pdev->dev).init_name)))
		return -EINVAL;

	if (of_property_read_u32(np, "spacemit,udc-mode", &(pdata->mode)))
		return -EINVAL;

	if (of_property_read_u32(np, "spacemit,dev-id", &(pdata->id)))
		pdata->id = PXA_USB_DEV_OTG;

	of_property_read_u32(np, "spacemit,extern-attr", &(pdata->extern_attr));
	pdata->otg_force_a_bus_req = of_property_read_bool(np,
					"spacemit,otg-force-a-bus-req");
	pdata->disable_otg_clock_gating = of_property_read_bool(np,
						"spacemit,disable-otg-clock-gating");

	return 0;
}

static int mv_ehci_probe(struct platform_device *pdev)
{
	struct mv_usb_platform_data *pdata;
	struct device *dev = &pdev->dev;
	struct device_node *node;
	struct usb_hcd *hcd;
	struct ehci_hcd *ehci;
	struct ehci_hcd_mv *ehci_mv;
	struct resource *r;
	int retval = -ENODEV;
	u32 offset;

	dev_dbg(&pdev->dev, "mv_ehci_probe: Enter ... \n");
	pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata) {
		dev_err(&pdev->dev, "failed to allocate memory for platform_data\n");
		return -ENODEV;
	}
	dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
	mv_ehci_dt_parse(pdev, pdata);
	/*
	 * Right now device-tree probed devices don't get dma_mask set.
	 * Since shared usb code relies on it, set it here for now.
	 * Once we have dma capability bindings this can go away.
	 */
	if (!dev->dma_mask)
		dev->dma_mask = &dev->coherent_dma_mask;
	if (!dev->coherent_dma_mask)
		dev->coherent_dma_mask = DMA_BIT_MASK(32);

	if (usb_disabled())
		return -ENODEV;

	hcd = usb_create_hcd(&mv_ehci_hc_driver, &pdev->dev, "mv ehci");
	if (!hcd)
		return -ENOMEM;

	ehci_mv = devm_kzalloc(&pdev->dev, sizeof(*ehci_mv), GFP_KERNEL);
	if (ehci_mv == NULL) {
		dev_err(&pdev->dev, "cannot allocate ehci_hcd_mv\n");
		retval = -ENOMEM;
		goto err_put_hcd;
	}

	platform_set_drvdata(pdev, ehci_mv);
	ehci_mv->pdata = pdata;
	ehci_mv->hcd = hcd;

	ehci_mv->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(ehci_mv->clk)) {
		dev_err(&pdev->dev, "error getting clock\n");
		retval = PTR_ERR(ehci_mv->clk);
		goto err_clear_drvdata;
	}
	clk_prepare(ehci_mv->clk);

	ehci_mv->reset = devm_reset_control_array_get_optional_shared(&pdev->dev);
	if (IS_ERR(ehci_mv->reset)) {
		dev_err(&pdev->dev, "failed to get reset control\n");
		retval = PTR_ERR(ehci_mv->reset);
		goto err_clear_drvdata;
	}

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!r) {
		dev_err(&pdev->dev, "no I/O memory resource defined\n");
		retval = -ENODEV;
		goto err_clear_drvdata;
	}

	ehci_mv->cap_regs = devm_ioremap(&pdev->dev, r->start,
					 resource_size(r));
	if (ehci_mv->cap_regs == NULL) {
		dev_err(&pdev->dev, "failed to map I/O memory\n");
		retval = -EFAULT;
		goto err_clear_drvdata;
	}

	ehci_mv->phy = devm_usb_get_phy_by_phandle(&pdev->dev, "usb-phy", 0);
	if (IS_ERR_OR_NULL(ehci_mv->phy)) {
		retval = PTR_ERR(ehci_mv->phy);
		if (retval != -EPROBE_DEFER && retval != -ENODEV)
			dev_err(&pdev->dev, "failed to get the outer phy\n");
		else {
			kfree(hcd->bandwidth_mutex);
			kfree(hcd);

			return -EPROBE_DEFER;
		}
		goto err_clear_drvdata;
	}

	ehci_clock_enable(ehci_mv);

	retval = reset_control_deassert(ehci_mv->reset);
	if (retval) {
		dev_err(&pdev->dev, "reset error %d\n", retval);
		goto err_disable_clk_rst;
	}

	retval = usb_phy_init(ehci_mv->phy);
	if (retval) {
		dev_err(&pdev->dev, "init phy error %d\n", retval);
		goto err_disable_clk_rst;
	}

	offset = readl(ehci_mv->cap_regs) & CAPLENGTH_MASK;
	ehci_mv->op_regs =
		(void __iomem *) ((unsigned long) ehci_mv->cap_regs + offset);
	hcd->rsrc_start = r->start;
	hcd->rsrc_len = resource_size(r);
	hcd->regs = ehci_mv->op_regs;

	hcd->irq = platform_get_irq(pdev, 0);
	if (!hcd->irq) {
		dev_err(&pdev->dev, "Cannot get irq.");
		retval = -ENODEV;
		goto err_disable_clk_rst;
	}

	ehci = hcd_to_ehci(hcd);
	ehci->caps = (struct ehci_caps *) ehci_mv->cap_regs;

	ehci_mv->mode = pdata->mode;

	node = of_find_compatible_node(NULL, NULL, "spacemit,spacemit-apmu");
	BUG_ON(!node);
	ehci_mv->apmu_base = of_iomap(node, 0);
	if (ehci_mv->apmu_base == NULL) {
		dev_err(&pdev->dev, "failed to map apmu base memory\n");
		goto err_disable_clk_rst;
	}

	if (ehci_mv->mode == MV_USB_MODE_OTG) {
		pr_info("ehci_mv  MV_USB_MODE_OTG ... \n");
		ehci_mv->otg = devm_usb_get_phy_by_phandle(&pdev->dev, "usb-otg", 0);
		if (IS_ERR(ehci_mv->otg)) {
			retval = PTR_ERR(ehci_mv->otg);

			if (retval == -ENXIO)
				dev_info(&pdev->dev, "MV_USB_MODE_OTG "
						"must have CONFIG_USB_PHY enabled\n");
			else if (retval != -EPROBE_DEFER)
				dev_err(&pdev->dev,
						"unable to find transceiver\n");
			goto err_disable_clk_rst;
		}

		retval = otg_set_host(ehci_mv->otg->otg, &hcd->self);
		if (retval < 0) {
			dev_err(&pdev->dev,
				"unable to register with transceiver\n");
			retval = -ENODEV;
			goto err_disable_clk_rst;
		}
		/* otg will enable clock before use as host */
		mv_ehci_disable(ehci_mv);
	} else {
		retval = ehci_otg_enable(dev, ehci_mv, 1);
		if (retval)
			goto err_disable_clk_rst;

		retval = usb_add_hcd(hcd, hcd->irq, IRQF_SHARED);
		if (retval) {
			dev_err(&pdev->dev,
				"failed to add hcd with err %d\n", retval);
			goto err_set_vbus;
		}
	}

	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	pm_suspend_ignore_children(dev, false);
	pm_runtime_get_sync(dev);

	dev_dbg(&pdev->dev,
		 "successful find EHCI device with regs 0x%p irq %d"
		 " working in %s mode\n", hcd->regs, hcd->irq,
		 ehci_mv->mode == MV_USB_MODE_OTG ? "OTG" : "Host");

	return 0;

err_set_vbus:
	ehci_otg_enable(dev, ehci_mv, 0);
err_disable_clk_rst:
	mv_ehci_disable(ehci_mv);
err_clear_drvdata:
	platform_set_drvdata(pdev, NULL);
err_put_hcd:
	usb_put_hcd(hcd);

	return retval;
}

static int mv_ehci_remove(struct platform_device *pdev)
{
	struct ehci_hcd_mv *ehci_mv = platform_get_drvdata(pdev);
	struct usb_hcd *hcd = ehci_mv->hcd;

	if (hcd->rh_registered)
		usb_remove_hcd(hcd);

	if (!IS_ERR_OR_NULL(ehci_mv->otg))
		otg_set_host(ehci_mv->otg->otg, NULL);

	if (ehci_mv->mode == MV_USB_MODE_HOST) {
		ehci_otg_enable(&pdev->dev, ehci_mv, 0);
		mv_ehci_disable(ehci_mv);
		clk_unprepare(ehci_mv->clk);
	}

	usb_put_hcd(hcd);

	pm_runtime_disable(&pdev->dev);
	pm_runtime_put_noidle(&pdev->dev);
	pm_runtime_set_suspended(&pdev->dev);

	return 0;
}

MODULE_ALIAS("mv-ehci");

static const struct of_device_id mv_ehci_dt_match[] = {
	{.compatible = "spacemit,mv-ehci"},
	{},
};
MODULE_DEVICE_TABLE(of, mv_ehci_dt_match);

static void mv_ehci_shutdown(struct platform_device *pdev)
{
	struct ehci_hcd_mv *ehci_mv = platform_get_drvdata(pdev);
	struct usb_hcd *hcd = ehci_mv->hcd;

	if (!hcd->rh_registered)
		return;

	if (hcd->driver->shutdown)
		hcd->driver->shutdown(hcd);
}

#ifdef CONFIG_PM_SLEEP
static int mv_ehci_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct ehci_hcd_mv *ehci_mv = platform_get_drvdata(pdev);
	struct usb_hcd *hcd = ehci_mv->hcd;
	bool do_wakeup = device_may_wakeup(dev);
	int ret;

	ret = ehci_suspend(hcd, do_wakeup);
	if (ret)
		return ret;
	usb_phy_shutdown(ehci_mv->phy);
	clk_disable_unprepare(ehci_mv->clk);
	dev_dbg(dev, "pm suspend: disable clks and phy\n");
	return ret;
}

static int mv_ehci_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct ehci_hcd_mv *ehci_mv = platform_get_drvdata(pdev);
	struct usb_hcd *hcd = ehci_mv->hcd;
	int ret;

	ret = clk_prepare_enable(ehci_mv->clk);
	if (ret){
		dev_err(dev, "Failed to enable clock");
		return ret;
	}
	ret = usb_phy_init(ehci_mv->phy);
	if (ret) {
		dev_err(dev, "Failed to init phy\n");
		ehci_clock_disable(ehci_mv);
		return ret;
	}
	dev_dbg(dev, "pm resume: do EHCI resume\n");
	ehci_resume(hcd, false);
	return 0;
}
#else
#define mv_ehci_suspend	NULL
#define mv_ehci_resume	NULL
#endif /* CONFIG_PM_SLEEP */

static const struct dev_pm_ops mv_ehci_pm_ops = {
	.suspend	= mv_ehci_suspend,
	.resume		= mv_ehci_resume,
};

static struct platform_driver ehci_k1x_driver = {
	.probe = mv_ehci_probe,
	.remove = mv_ehci_remove,
	.shutdown = mv_ehci_shutdown,
	.driver = {
		   .name = "mv-ehci",
		   .of_match_table = of_match_ptr(mv_ehci_dt_match),
		   .bus = &platform_bus_type,
#ifdef CONFIG_PM
		   .pm	= &mv_ehci_pm_ops,
#endif
		   },
};
