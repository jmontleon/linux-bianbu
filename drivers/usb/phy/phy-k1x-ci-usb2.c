// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * UDC Phy support for Spacemit k1x SoCs
 *
 * Copyright (c) 2023 Spacemit Inc.
 */

#include <linux/resource.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/io.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/export.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/platform_data/k1x_ci_usb.h>
#include <linux/usb/phy.h>
#include <linux/of_address.h>
#include "phy-k1x-ci-usb2.h"

/* phy regs */

#define USB2_PHY_REG01			0x4
#define USB2_PHY_REG01_PLL_IS_READY	(0x1 << 0)
#define USB2_PHY_REG04			0x10
#define USB2_PHY_REG04_EN_HSTSOF	(0x1 << 0)
#define USB2_PHY_REG04_AUTO_CLEAR_DIS	(0x1 << 2)
#define USB2_PHY_REG08			0x20
#define USB2_PHY_REG08_DISCON_DET	(0x1 << 9)
#define USB2_PHY_REG0D			0x34
#define USB2_PHY_REG26			0x98
#define USB2_PHY_REG22			0x88
#define USB2_CFG_FORCE_CDRCLK		(0x1 << 6)
#define USB2_PHY_REG06			0x18
#define USB2_CFG_HS_SRC_SEL		(0x1 << 0)

#define USB2D_CTRL_RESET_TIME_MS	50

static struct mv_usb2_phy *mv_phy_ptr;

static int mv_usb2_phy_init(struct usb_phy *phy)
{
	struct mv_usb2_phy *mv_phy = container_of(phy, struct mv_usb2_phy, phy);
	void __iomem *base = mv_phy->base;
	uint32_t loops, temp;

	clk_enable(mv_phy->clk);

	// make sure the usb controller is not under reset process before any configuration
	udelay(50);
	writel(0xbec4, base + USB2_PHY_REG26); //24M ref clk
	udelay(150);

	loops = USB2D_CTRL_RESET_TIME_MS * 1000;

	//wait for usb2 phy PLL ready
	do {
		temp = readl(base + USB2_PHY_REG01);
		if (temp & USB2_PHY_REG01_PLL_IS_READY)
			break;
		udelay(50);
	} while(--loops);

	if (loops == 0)
		pr_info("Wait PHY_REG01[PLLREADY] timeout\n");

	//release usb2 phy internal reset and enable clock gating
	writel(0x60ef, base + USB2_PHY_REG01);
	writel(0x1c, base + USB2_PHY_REG0D);

	//select HS parallel data path
	temp = readl(base + USB2_PHY_REG06);
	// temp |= USB2_CFG_HS_SRC_SEL;
	temp &= ~(USB2_CFG_HS_SRC_SEL);
	writel(temp, base + USB2_PHY_REG06);

	/* auto clear host disc*/
	temp = readl(base + USB2_PHY_REG04);
	temp |= USB2_PHY_REG04_AUTO_CLEAR_DIS;
	writel(temp, base + USB2_PHY_REG04);

	return 0;
}

static void mv_usb2_phy_shutdown(struct usb_phy *phy)
{
	struct mv_usb2_phy *mv_phy = container_of(phy, struct mv_usb2_phy, phy);

	clk_disable(mv_phy->clk);
}

static const struct of_device_id mv_usbphy_dt_match[];

static int mv_usb2_get_phydata(struct platform_device *pdev,
				struct mv_usb2_phy *mv_phy)
{
	struct device_node *np = pdev->dev.of_node;
	const struct of_device_id *match;
	u32 phy_rev;

	match = of_match_device(mv_usbphy_dt_match, &pdev->dev);
	if (!match)
		return -ENODEV;

	mv_phy->drv_data.phy_type = (unsigned long)match->data;

	if (!of_property_read_u32(np, "spacemit,usb2-phy-rev", &phy_rev))
		mv_phy->drv_data.phy_rev = phy_rev;
	else
		dev_info(&pdev->dev, "No PHY revision found, use the default setting!");

	return 0;
}

static int mv_usb2_phy_probe(struct platform_device *pdev)
{
	struct mv_usb2_phy *mv_phy;
	struct resource *r;
	int ret = 0;

	dev_dbg(&pdev->dev, "k1x-ci-usb-phy-probe: Enter...\n");
	mv_phy = devm_kzalloc(&pdev->dev, sizeof(*mv_phy), GFP_KERNEL);
	if (mv_phy == NULL) {
		dev_err(&pdev->dev, "failed to allocate memory\n");
		return -ENOMEM;
	}

	mv_phy->pdev = pdev;

	ret = mv_usb2_get_phydata(pdev, mv_phy);
	if (ret) {
		dev_err(&pdev->dev, "No matching phy founded\n");
		return ret;
	}

	mv_phy->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(mv_phy->clk)) {
		dev_err(&pdev->dev, "failed to get clock.\n");
		return PTR_ERR(mv_phy->clk);
	}
	clk_prepare(mv_phy->clk);

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (r == NULL) {
		dev_err(&pdev->dev, "no phy I/O memory resource defined\n");
		return -ENODEV;
	}
	mv_phy->base = devm_ioremap_resource(&pdev->dev, r);
	if (mv_phy->base == NULL) {
		dev_err(&pdev->dev, "error map register base\n");
		return -EBUSY;
	}

	mv_phy->phy.dev = &pdev->dev;
	mv_phy->phy.label = "mv-usb2";
	mv_phy->phy.type = USB_PHY_TYPE_USB2;
	mv_phy->phy.init = mv_usb2_phy_init;
	mv_phy->phy.shutdown = mv_usb2_phy_shutdown;

	usb_add_phy_dev(&mv_phy->phy);

	platform_set_drvdata(pdev, mv_phy);

	mv_phy_ptr = mv_phy;

	return 0;
}

static int mv_usb2_phy_remove(struct platform_device *pdev)
{
	struct mv_usb2_phy *mv_phy = platform_get_drvdata(pdev);

	usb_remove_phy(&mv_phy->phy);

	clk_unprepare(mv_phy->clk);

	platform_set_drvdata(pdev, NULL);

	return 0;
}

static const struct of_device_id mv_usbphy_dt_match[] = {
	{ .compatible = "spacemit,usb2-phy",},
	{},
};
MODULE_DEVICE_TABLE(of, mv_usbphy_dt_match);

static struct platform_driver mv_usb2_phy_driver = {
	.probe	= mv_usb2_phy_probe,
	.remove = mv_usb2_phy_remove,
	.driver = {
		.name   = "mv-usb2-phy",
		.owner  = THIS_MODULE,
		.of_match_table = of_match_ptr(mv_usbphy_dt_match),
	},
};

module_platform_driver(mv_usb2_phy_driver);
MODULE_DESCRIPTION("Spacemit USB2 phy driver");
MODULE_LICENSE("GPL v2");
