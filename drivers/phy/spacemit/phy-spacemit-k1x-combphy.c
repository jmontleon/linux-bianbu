// SPDX-License-Identifier: GPL-2.0
/*
 * phy-spacemit-k1x-combphy.c - Spacemit k1-x combo PHY for USB3 and PCIE
 *
 * Copyright (c) 2023 Spacemit Co., Ltd.
 *
 */

#include <dt-bindings/phy/phy.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/of_device.h>
#include <linux/clk.h>
#include <linux/reset.h>
#include <linux/delay.h>

#define SPACEMIT_COMBPHY_WAIT_TIMEOUT 1000
#define SPACEMIT_COMBPHY_MODE_SEL BIT(3)

// Registers for USB3 PHY
#define SPACEMIT_COMBPHY_USB_REG1 0x68
#define SPACEMIT_COMBPHY_USB_REG1_VAL 0x0
#define SPACEMIT_COMBPHY_USB_REG2 (0x12 << 2)
#define SPACEMIT_COMBPHY_USB_REG2_VAL 0x603a2276
#define SPACEMIT_COMBPHY_USB_REG3 (0x02 << 2)
#define SPACEMIT_COMBPHY_USB_REG3_VAL 0x97c
#define SPACEMIT_COMBPHY_USB_REG4 (0x06 << 2)
#define SPACEMIT_COMBPHY_USB_REG4_VAL 0x0
#define SPACEMIT_COMBPHY_USB_PLL_REG 0x8
#define SPACEMIT_COMBPHY_USB_PLL_MASK 0x1
#define SPACEMIT_COMBPHY_USB_PLL_VAL 0x1
#define SPACEMIT_COMBPHY_USB_TERM_SHORT 0x3000

struct spacemit_combphy_priv;

struct spacemit_combphy_priv {
	struct device *dev;
	int num_clks;
	struct clk_bulk_data *clks;
	struct reset_control *phy_rst;
	void __iomem *phy_sel;
	void __iomem *puphy_base;
	struct phy *phy;
	u8 type;
	bool suspend_term_quirk;
};

static inline void spacemit_reg_updatel(void __iomem *reg, u32 offset, u32 mask,
					u32 val)
{
	u32 tmp;
	tmp = readl(reg + offset);
	tmp = (tmp & ~(mask)) | val;
	writel(tmp, reg + offset);
}

static int spacemit_combphy_wait_ready(struct spacemit_combphy_priv *priv,
					   u32 offset, u32 mask, u32 val)
{
	int timeout = SPACEMIT_COMBPHY_WAIT_TIMEOUT;
	while (((readl(priv->puphy_base + offset) & mask) != val) && --timeout)
		;
	if (!timeout) {
		return -ETIMEDOUT;
	}
	dev_dbg(priv->dev, "phy init timeout remain: %d", timeout);
	return 0;
}

static int spacemit_combphy_set_mode(struct spacemit_combphy_priv *priv)
{
	u8 mode = priv->type;
	if (mode == PHY_TYPE_USB3) {
		spacemit_reg_updatel(priv->phy_sel, 0, 0,
					 SPACEMIT_COMBPHY_MODE_SEL);
	} else {
		dev_err(priv->dev, "PHY type %x not supported\n", mode);
		return -EINVAL;
	}
	return 0;
}

static int spacemit_combphy_init_usb(struct spacemit_combphy_priv *priv)
{
	int ret;
	void __iomem *base = priv->puphy_base;
	dev_info(priv->dev, "USB3 PHY init.\n");

	writel(SPACEMIT_COMBPHY_USB_REG1_VAL, base + SPACEMIT_COMBPHY_USB_REG1);
	writel(SPACEMIT_COMBPHY_USB_REG2_VAL, base + SPACEMIT_COMBPHY_USB_REG2);
	writel(SPACEMIT_COMBPHY_USB_REG3_VAL, base + SPACEMIT_COMBPHY_USB_REG3);
	writel(SPACEMIT_COMBPHY_USB_REG4_VAL, base + SPACEMIT_COMBPHY_USB_REG4);

	ret = spacemit_combphy_wait_ready(priv, SPACEMIT_COMBPHY_USB_PLL_REG,
					  SPACEMIT_COMBPHY_USB_PLL_MASK,
					  SPACEMIT_COMBPHY_USB_PLL_VAL);

	if (priv->suspend_term_quirk) {
		spacemit_reg_updatel(base, 0x18, 0, SPACEMIT_COMBPHY_USB_TERM_SHORT);
	}

	if (ret)
		dev_err(priv->dev, "USB3 PHY init timeout!\n");

	return ret;
}

static int spacemit_combphy_init(struct phy *phy)
{
	struct spacemit_combphy_priv *priv = phy_get_drvdata(phy);
	int ret;

	ret = spacemit_combphy_set_mode(priv);

	if (ret) {
		dev_err(priv->dev, "failed to set mode for PHY type %x\n",
			priv->type);
		goto out;
	}

	ret = clk_bulk_prepare_enable(priv->num_clks, priv->clks);
	if (ret) {
		dev_err(priv->dev, "failed to enable clks\n");
		goto out;
	}

	ret = reset_control_deassert(priv->phy_rst);
	if (ret) {
		dev_err(priv->dev, "failed to deassert rst\n");
		goto err_clk;
	}

	switch (priv->type) {
	case PHY_TYPE_USB3:
		ret = spacemit_combphy_init_usb(priv);
		break;
	default:
		dev_err(priv->dev, "PHY type %x not supported\n", priv->type);
		ret = -EINVAL;
		break;
	}

	if (ret)
		goto err_rst;

	return 0;

err_rst:
	reset_control_assert(priv->phy_rst);
err_clk:
	clk_bulk_disable_unprepare(priv->num_clks, priv->clks);
out:
	return ret;
}

static int spacemit_combphy_exit(struct phy *phy)
{
	struct spacemit_combphy_priv *priv = phy_get_drvdata(phy);

	clk_bulk_disable_unprepare(priv->num_clks, priv->clks);
	reset_control_assert(priv->phy_rst);

	return 0;
}

static struct phy *spacemit_combphy_xlate(struct device *dev,
					  struct of_phandle_args *args)
{
	struct spacemit_combphy_priv *priv = dev_get_drvdata(dev);

	if (args->args_count != 1) {
		dev_err(dev, "invalid number of arguments\n");
		return ERR_PTR(-EINVAL);
	}

	if (priv->type != PHY_NONE && priv->type != args->args[0])
		dev_warn(dev, "PHY type %d is selected to override %d\n",
			 args->args[0], priv->type);

	priv->type = args->args[0];

	if (args->args_count > 1) {
		dev_dbg(dev, "combo phy idx: %d selected",  args->args[1]);
	}

	return priv->phy;
}

static const struct phy_ops spacemit_combphy_ops = {
	.init = spacemit_combphy_init,
	.exit = spacemit_combphy_exit,
	.owner = THIS_MODULE,
};

static int spacemit_combphy_probe(struct platform_device *pdev)
{
	struct phy_provider *phy_provider;
	struct device *dev = &pdev->dev;
	struct spacemit_combphy_priv *priv;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->puphy_base = devm_platform_ioremap_resource_byname(pdev, "puphy");
	if (IS_ERR(priv->puphy_base)) {
		ret = PTR_ERR(priv->puphy_base);
		return ret;
	}

	priv->phy_sel = devm_platform_ioremap_resource_byname(pdev, "phy_sel");
	if (IS_ERR(priv->phy_sel)) {
		ret = PTR_ERR(priv->phy_sel);
		return ret;
	}
	priv->suspend_term_quirk = device_property_read_bool(&pdev->dev, "suspend-term-quirk");
	priv->dev = dev;
	priv->type = PHY_NONE;

	priv->num_clks = devm_clk_bulk_get_all(dev, &priv->clks);

	priv->phy_rst = devm_reset_control_array_get_shared(dev);
	if (IS_ERR(priv->phy_rst))
		return dev_err_probe(dev, PTR_ERR(priv->phy_rst),
					 "failed to get phy reset\n");

	priv->phy = devm_phy_create(dev, NULL, &spacemit_combphy_ops);
	if (IS_ERR(priv->phy)) {
		dev_err(dev, "failed to create combphy\n");
		return PTR_ERR(priv->phy);
	}

	dev_set_drvdata(dev, priv);
	phy_set_drvdata(priv->phy, priv);

	phy_provider =
		devm_of_phy_provider_register(dev, spacemit_combphy_xlate);

	return PTR_ERR_OR_ZERO(phy_provider);
}

static const struct of_device_id spacemit_combphy_of_match[] = {
	{
		.compatible = "spacemit,k1x-combphy",
	},
	{},
};
MODULE_DEVICE_TABLE(of, spacemit_combphy_of_match);

static struct platform_driver spacemit_combphy_driver = {
	.probe	= spacemit_combphy_probe,
	.driver = {
		.name = "spacemit-k1x-combphy",
		.of_match_table = spacemit_combphy_of_match,
	},
};
module_platform_driver(spacemit_combphy_driver);
