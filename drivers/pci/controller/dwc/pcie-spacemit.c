// SPDX-License-Identifier: GPL-2.0
/*
 * Spacemit PCIe root complex driver
 *
 * Copyright (c) 2023, spacemit Corporation.
 *
 */
#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/pci.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/resource.h>
#include <linux/reset.h>

#include "pcie-designware.h"

#define  PCIE_APP_TYPE         0x000
#define  DEVICE_TYPE_RC      0x4

#define  PCIE_APP_CTL          0x004
#define  CTL_LTSSM_ENABLE    BIT(0)

#define  PCIE_APP_INTEN        0x100
#define  PCIE_APP_INTSTA       0x104
#define  PCIE_APP_STATE        0x200
#define  LTSSM_STATE_MASK    GENMASK(5, 0)

struct spacemit_pcie {
	struct dw_pcie		*pci;
	void __iomem		*app_base;
	struct phy		*phy;
	struct clk		*clk;
	struct reset_control *reset;
};

#define to_spacemit_pcie(x)		dev_get_drvdata((x)->dev)

static int spacemit_pcie_start_link(struct dw_pcie *pci)
{
	struct spacemit_pcie *spacemit_pcie = to_spacemit_pcie(pci);
        u64 ctl_reg = spacemit_pcie->app_base + PCIE_APP_CTL;

        writel(readl(ctl_reg) | CTL_LTSSM_ENABLE, ctl_reg);
	return 0;
}

static int spacemit_pcie_link_up(struct dw_pcie *pci)
{
        volatile u32 ltssm_state = 0;
        struct spacemit_pcie *spacemit_pcie = to_spacemit_pcie(pci);
	u64 state_reg = spacemit_pcie->app_base + PCIE_APP_STATE;

        ltssm_state = readl(state_reg) & LTSSM_STATE_MASK;
	return !!ltssm_state;
}

static irqreturn_t spacemit_pcie_irq_handler(int irq, void *arg)
{
	struct spacemit_pcie *spacemit_pcie = arg;
	struct dw_pcie *pci = spacemit_pcie->pci;
	struct dw_pcie_rp *pp = &pci->pp;
	u64    intsta_reg = 0;
	unsigned int status;

        intsta_reg = spacemit_pcie->app_base + PCIE_APP_INTSTA;
	status = readl(intsta_reg);

	if (status & 3) {
		BUG_ON(!IS_ENABLED(CONFIG_PCI_MSI));
		dw_handle_msi_irq(pp);
	}

	writel(status, intsta_reg);

	return IRQ_HANDLED;
}

static void spacemit_pcie_enable_interrupts(struct spacemit_pcie *spacemit_pcie)
{
	u64 inten_reg = spacemit_pcie->app_base + PCIE_APP_INTEN;
}

static int spacemit_pcie_host_init(struct dw_pcie_rp *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct spacemit_pcie *spacemit_pcie = to_spacemit_pcie(pci);
        int ret;

        if (!IS_ERR(spacemit_pcie->clk)) {
                ret = clk_prepare_enable(spacemit_pcie->clk);
	        if (ret) {
		        printk(KERN_ERR "couldn't enable clk for pcie\n");
		        return ret;
	        }
	}

	reset_control_deassert(spacemit_pcie->reset);

	//set rc mode
        writel(DEVICE_TYPE_RC, spacemit_pcie->app_base + PCIE_APP_TYPE);
	return 0;
}

static const struct dw_pcie_host_ops spacemit_pcie_host_ops = {
	.host_init = spacemit_pcie_host_init,
};

static const struct dw_pcie_ops dw_pcie_ops = {
	.link_up = spacemit_pcie_link_up,
	.start_link = spacemit_pcie_start_link,
};

static int spacemit_pcie_probe(struct platform_device *pdev)
{
        struct device *dev = &pdev->dev;
	struct dw_pcie_rp *pp;
	struct dw_pcie *pci;
	struct spacemit_pcie *spacemit_pcie;
	struct device_node *np = dev->of_node;
	int ret;

        spacemit_pcie = devm_kzalloc(dev, sizeof(*spacemit_pcie), GFP_KERNEL);
	if (!spacemit_pcie)
		return -ENOMEM;

        pci = devm_kzalloc(dev, sizeof(*pci), GFP_KERNEL);
	if (!pci)
		return -ENOMEM;

        pci->dev = dev;
	pci->ops = &dw_pcie_ops;
	pp = &pci->pp;

	spacemit_pcie->pci = pci;

        pm_runtime_enable(dev);
	ret = pm_runtime_get_sync(dev);
	if (ret < 0)
		goto err_pm_runtime_put;

        spacemit_pcie->app_base = devm_platform_ioremap_resource_byname(pdev, "app");
	if (IS_ERR(spacemit_pcie->app_base)) {
        ret = PTR_ERR(spacemit_pcie->app_base);
		goto err_pm_runtime_put;
	}

        spacemit_pcie->phy = devm_phy_optional_get(dev, "pciephy");
	if (IS_ERR(spacemit_pcie->phy)) {
		ret = PTR_ERR(spacemit_pcie->phy);
		goto err_pm_runtime_put;
	}

        ret = phy_init(spacemit_pcie->phy);
	if (ret)
		goto err_pm_runtime_put;

        spacemit_pcie->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(spacemit_pcie->clk)) {
		dev_err(dev, "pcie clk not exist, skipped it\n");
	}

        spacemit_pcie->reset = devm_reset_control_array_get_optional_exclusive(&pdev->dev);
	if (IS_ERR(spacemit_pcie->reset)) {
		dev_err(dev, "Failed to get pcie's resets\n");
		ret = PTR_ERR(spacemit_pcie->reset);
		goto err_phy_exit;
        }

        platform_set_drvdata(pdev, spacemit_pcie);

        pp->ops = &spacemit_pcie_host_ops;

	ret = dw_pcie_host_init(pp);
	if (ret < 0) {
		dev_err(dev, "failed to initialize host\n");
		goto err_phy_exit;
	}

	return 0;

err_phy_exit:
	phy_exit(spacemit_pcie->phy);
err_pm_runtime_put:
	pm_runtime_put(dev);
	pm_runtime_disable(dev);

    return ret;
}


static const struct of_device_id spacemit_pcie_of_match[] = {
	{ .compatible = "spacemit,k1-pro-pcie"},
	{},
};

static struct platform_driver spacemit_pcie_driver = {
	.probe		= spacemit_pcie_probe,
	.driver = {
		.name	= "spacemit-pcie",
		.of_match_table = spacemit_pcie_of_match,
		.suppress_bind_attrs = true,
	},
};

builtin_platform_driver(spacemit_pcie_driver);
