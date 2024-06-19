// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for SPACEMIT K1X IPE MODULE
 *
 * Copyright(C) 2023 SPACEMIT Micro Limited.
 */
#define DEBUG			/* for pr_debug() */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/errno.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/interrupt.h>
#include <media/v4l2-common.h>
#include <media/v4l2-dev.h>
#include <media/videobuf2-dma-contig.h>
#include <media/videobuf2-dma-sg.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include "ccic_drv.h"
#include "ccic_hwreg.h"
#include "csiphy.h"

#ifdef CONFIG_ARCH_ZYNQMP
#include "dptc_drv.h"
#include "dptc_pll_setting.h"
#endif

#define K1X_CCIC_DRV_NAME "k1xccic"

static LIST_HEAD(ccic_devices);
static DEFINE_MUTEX(list_lock);

static void ccic_irqmask(struct ccic_ctrl *ctrl, int on)
{
	struct ccic_dev *ccic_dev = ctrl->ccic_dev;
	u32 mask_val = ccic_dev->interrupt_mask_value;

	if (on) {
		ccic_reg_write(ccic_dev, REG_IRQSTAT, mask_val);
		ccic_reg_set_bit(ccic_dev, REG_IRQMASK, mask_val);
	} else {
		ccic_reg_clear_bit(ccic_dev, REG_IRQMASK, mask_val);
	}
}

#ifndef CONFIG_ARCH_ZYNQMP
static int ccic_config_csi2(struct ccic_dev *ccic_dev, struct mipi_csi2 *csi,
			    int enable)
{
	unsigned int dphy5_val = 0;
	unsigned int ctrl0_val = 0;
	int lanes = csi->dphy_desc.nr_lane;

	if (!ccic_dev->csiphy)
		return -EINVAL;

	if (enable) {
		csiphy_start(ccic_dev->csiphy, csi);
		dphy5_val = CSI2_DPHY5_LANE_ENA(lanes);
		dphy5_val = dphy5_val | (dphy5_val << CSI2_DPHY5_LANE_RESC_ENA_SHIFT);
		ctrl0_val = ccic_reg_read(ccic_dev, REG_CSI2_CTRL0);
		ctrl0_val &= ~(CSI2_C0_LANE_NUM_MASK);
		ctrl0_val |= CSI2_C0_LANE_NUM(lanes);
		ctrl0_val |= CSI2_C0_ENABLE;
		ctrl0_val &= ~(CSI2_C0_VLEN_MASK);
		ctrl0_val |= CSI2_C0_VLEN;
		ccic_reg_write(ccic_dev, REG_CSI2_DPHY5, dphy5_val);
		ccic_reg_write(ccic_dev, REG_CSI2_CTRL0, ctrl0_val);
	} else {
		csiphy_stop(ccic_dev->csiphy);
		ccic_reg_clear_bit(ccic_dev, REG_CSI2_CTRL0, CSI2_C0_ENABLE);	//csi off
	}

	return 0;
}

#else /* connected to DPTC daughter board */
/*ipe_fpga_rstn_adr*/
#define REG_CSI2_FPGA_RSTN	(0x1fc)
static int ccic_config_csi2(struct ccic_dev *ccic_dev,
			    struct mipi_csi2 *csi, int enable)
{
	unsigned int ctrl0_val = 0;
	int lanes = 1;
	unsigned int sensor_width = 0, sensor_height = 0;

	if (!enable)
		goto out;

	ctrl0_val = ccic_reg_read(ccic_dev, REG_CSI2_CTRL0);
	ctrl0_val &= ~(CSI2_C0_LANE_NUM_MASK);
	ctrl0_val |= CSI2_C0_LANE_NUM(lanes);
	ctrl0_val |= CSI2_C0_ENABLE;
	ctrl0_val &= ~(CSI2_C0_VLEN_MASK);
	ctrl0_val |= CSI2_C0_VLEN;
	ccic_reg_write(ccic_dev, REG_CSI2_CTRL0, ctrl0_val);

out:
	if (!enable) {
		ccic_reg_clear_bit(ccic_dev, REG_CSI2_CTRL0, CSI2_C0_ENABLE);	//csi off
		DPTC_func3_close();
	} else {
		DPTC_func3_open();
		/* csi_ccic_fpga_reset:bit[5:0] = 00 */
		ccic_reg_write_mask(ccic_dev, REG_CSI2_FPGA_RSTN, 0, 0x3f);
		/* udelay(10); */
		dptc_csi_reg_setting(16, sensor_width, sensor_height, lanes);
		/*csi_ccic_fpga_release: bit[5:0] = b11 */
		ccic_reg_write_mask(ccic_dev, REG_CSI2_FPGA_RSTN, 0x3f, 0x3f);
		/* udelay(10); */
	}

	return 0;
}
#endif

static int ccic_config_csi2_dphy(struct ccic_ctrl *ctrl,
				 struct mipi_csi2 *csi, int enable)
{
	int ret = 0;
	struct ccic_dev *ccic_dev = ctrl->ccic_dev;

	ret = ccic_config_csi2(ccic_dev, csi, enable);
	if (ret)
		dev_err(ccic_dev->dev, "csi2 config failed\n");

	return ret;
}

static int ccic_config_csi2_vc(struct ccic_ctrl *ctrl, int md, u8 vc0, u8 vc1)
{
	int ret = 0;
	struct ccic_dev *ccic_dev = ctrl->ccic_dev;

	switch (md) {
	case CCIC_CSI2VC_NM:	/* Normal mode */
		ccic_reg_write_mask(ccic_dev, REG_CSI2_VCCTRL,
				    CSI2_VCCTRL_MD_NORMAL, CSI2_VCCTRL_MD_MASK);
		break;
	case CCIC_CSI2VC_VC:	/* Virtual Channel mode */
		ccic_reg_write_mask(ccic_dev, REG_CSI2_VCCTRL,
				    CSI2_VCCTRL_MD_VC, CSI2_VCCTRL_MD_MASK);
		ccic_reg_write_mask(ccic_dev, REG_CSI2_VCCTRL, vc0 << 14,
				    CSI2_VCCTRL_VC0_MASK);
		ccic_reg_write_mask(ccic_dev, REG_CSI2_VCCTRL, vc1 << 22,
				    CSI2_VCCTRL_VC1_MASK);
		break;
	case CCIC_CSI2VC_DT:	/* TODO: Data-Type Interleaving */
		ccic_reg_write_mask(ccic_dev, REG_CSI2_VCCTRL,
				    CSI2_VCCTRL_MD_DT, CSI2_VCCTRL_MD_MASK);
		pr_err("csi2 vc mode %d todo\n", md);
		break;
	default:
		dev_err(ccic_dev->dev, "invalid csi2 vc mode %d\n", md);
		ret = -EINVAL;
	}

	return ret;
}

/**
 * ccic_config_idi_mux - DPCM/Repack Mux Select
 *
 * @ctrl: ccic controller
 * @mux: ipe mux path
 *
 * Return: 0 on success, error code otherwise.
 */
static int ccic_config_idi_mux(struct ccic_ctrl *ctrl, int mux)
{
	int ret = 0;
	struct ccic_dev *ccic_dev = ctrl->ccic_dev;

	/*
	 * IPE1-->
	 * 0x0 Select "local" CSI2 main output
	 * 0x1 Select "IPE2" CSI2 VC/DT output
	 * 0x2 Select "IPE2" CSI2 main output
	 * 0x3 Select "IPE3" CSI2 VC/DT output
	 *
	 * IPE3-->
	 * 0x0 Select "local" CSI2 main output
	 * 0x1 Select "IPE2" CSI2 VC/DT output
	 * 0x2 Select "IPE2" CSI2 main output
	 * 0x3 Select "IPE1" CSI2 VC/DT output
	 */
	switch (mux) {
	case CCIC_IDI_MUX_LOCAL_MAIN:
		ccic_reg_write_mask(ccic_dev, REG_CSI2_CTRL2,
				    CSI2_C2_MUX_SEL_LOCAL_MAIN, CSI2_C2_MUX_SEL_MASK);
		break;
	case CCIC_IDI_MUX_IPE2_VCDT:
		ccic_reg_write_mask(ccic_dev, REG_CSI2_CTRL2,
				    CSI2_C2_MUX_SEL_IPE2_VCDT, CSI2_C2_MUX_SEL_MASK);
		break;
	case CCIC_IDI_MUX_IPE2_MAIN:
		ccic_reg_write_mask(ccic_dev, REG_CSI2_CTRL2,
				    CSI2_C2_MUX_SEL_IPE2_MAIN, CSI2_C2_MUX_SEL_MASK);
		break;
	case CCIC_IDI_MUX_REMOTE_VCDT:
		ccic_reg_write_mask(ccic_dev, REG_CSI2_CTRL2,
				    CSI2_C2_MUX_SEL_REMOTE_VCDT, CSI2_C2_MUX_SEL_MASK);
		break;
	default:
		dev_err(ccic_dev->dev, "invalid idi mux %d\n", mux);
		ret = -EINVAL;
	}

	return ret;
}

static int ccic_config_idi_sel(struct ccic_ctrl *ctrl, int sel)
{
	int ret = 0;
	struct ccic_dev *ccic_dev = ctrl->ccic_dev;

	switch (sel) {
	case CCIC_IDI_SEL_NONE:
		/* ccic_reg_clear_bit(ccic_dev, REG_IDI_CTRL, IDI_RELEASE_RESET); */
		ccic_reg_set_bit(ccic_dev, REG_CSI2_CTRL2, CSI2_C2_REPACK_RST);
		ccic_reg_clear_bit(ccic_dev, REG_CSI2_CTRL2, CSI2_C2_REPACK_ENA);
		ccic_reg_clear_bit(ccic_dev, REG_CSI2_CTRL2, CSI2_C2_DPCM_ENA);
		ccic_reg_clear_bit(ccic_dev, REG_CSI2_VCCTRL, CSI2_VCCTRL_MD_VC);
		ccic_reg_write_mask(ccic_dev, REG_CSI2_CTRL2,
				    CSI2_C2_MUX_SEL_LOCAL_MAIN, CSI2_C2_MUX_SEL_MASK);
		break;
	case CCIC_IDI_SEL_REPACK:
		ccic_reg_write_mask(ccic_dev, REG_IDI_CTRL, IDI_SEL_DPCM_REPACK,
				    IDI_SEL_MASK);
		ccic_reg_clear_bit(ccic_dev, REG_CSI2_CTRL2, CSI2_C2_IDI_MUX_SEL_DPCM);
		ccic_reg_clear_bit(ccic_dev, REG_CSI2_CTRL2, CSI2_C2_REPACK_RST);
		ccic_reg_set_bit(ccic_dev, REG_CSI2_CTRL2, CSI2_C2_REPACK_ENA);
		break;
	case CCIC_IDI_SEL_DPCM:
		ccic_reg_write_mask(ccic_dev, REG_IDI_CTRL,
				    IDI_SEL_DPCM_REPACK, IDI_SEL_MASK);
		ccic_reg_set_bit(ccic_dev, REG_CSI2_CTRL2, CSI2_C2_IDI_MUX_SEL_DPCM);
		ccic_reg_set_bit(ccic_dev, REG_CSI2_CTRL2, CSI2_C2_DPCM_ENA);
		break;
	case CCIC_IDI_SEL_PARALLEL:
		ccic_reg_write_mask(ccic_dev, REG_IDI_CTRL,
				    IDI_SEL_PARALLEL, IDI_SEL_MASK);
		break;
	default:
		dev_err(ccic_dev->dev, "IDI source is error %d\n", sel);
		ret = -EINVAL;
	}

	return ret;
}

static int __maybe_unused ccic_enable_csi2idi(struct ccic_ctrl *ctrl)
{
	struct ccic_dev *ccic_dev = ctrl->ccic_dev;

	ccic_csi2idi_reset(ccic_dev, 0);

	return 0;
}

static int __maybe_unused ccic_disable_csi2idi(struct ccic_ctrl *ctrl)
{
	struct ccic_dev *ccic_dev = ctrl->ccic_dev;

	ccic_csi2idi_reset(ccic_dev, 1);

	return 0;
}

#define ISP_BUS_CLK_FREQ (307200000)
static int axi_set_clock_rates(struct clk *clock)
{
	long rate;
	int ret;

	rate = clk_round_rate(clock, ISP_BUS_CLK_FREQ);
	if (rate < 0) {
		pr_err("axi clk round rate failed: %ld\n", rate);
		return -EINVAL;
	}

	ret = clk_set_rate(clock, rate);
	if (ret < 0) {
		pr_err("axi clk set rate failed: %d\n", ret);
		return ret;
	}

	return 0;
}

int ccic_dma_clk_enable(struct ccic_dma *dma, int on)
{
	struct ccic_dev *ccic = dma->ccic_dev;
	struct device *dev = &ccic->pdev->dev;
	int ret;

	if (on) {
		ret = pm_runtime_get_sync(dev);
		if (ret < 0)
			return ret;

		ret = clk_prepare_enable(ccic->axi_clk);
		if (ret < 0) {
			pm_runtime_put_sync(dev);
			return ret;
		}
		reset_control_deassert(ccic->isp_ci_reset);

		ret = axi_set_clock_rates(ccic->axi_clk);
		if (ret < 0) {
			pm_runtime_put_sync(dev);
			return ret;
		}
		reset_control_deassert(ccic->isp_ci_reset);
	} else {
		clk_disable_unprepare(ccic->axi_clk);
		reset_control_assert(ccic->isp_ci_reset);
		pm_runtime_put_sync(dev);
	}

	return 0;
}

static struct ccic_dma_ops ccic_dma_ops = {
	.clk_enable = ccic_dma_clk_enable,
};

/*
 * TBD: calculate the clk rate dynamically based on
 * fps, resolution and other arguments.
 */
static int ccic_clk_set_rate(struct ccic_ctrl *ctrl_dev, int mode)
{
	unsigned long clk_val;
	struct ccic_dev *ccic_dev = ctrl_dev->ccic_dev;

#if defined(CONFIG_SPACEMIT_ZEBU)
	clk_val = clk_round_rate(ccic_dev->csi_clk, 1248000000);
#else
	clk_val = clk_round_rate(ccic_dev->csi_clk, 624000000);
#endif

	clk_set_rate(ccic_dev->csi_clk, clk_val);
	pr_debug("cam clk[csi_func]: %ld\n", clk_val);

	if (mode == SC2_MODE_ISP) {
#if defined(CONFIG_SPACEMIT_ZEBU)
		clk_val = clk_round_rate(ccic_dev->clk4x, 1248000000);
#else
		clk_val = clk_round_rate(ccic_dev->csi_clk, 624000000);
#endif

		clk_set_rate(ccic_dev->clk4x, clk_val);
		pr_debug("cam clk[ccic4x_func]: %ld\n", clk_val);
	}

	return 0;
}

int ccic_clk_enable(struct ccic_ctrl *ctrl, int en)
{
	int ret = 0;
	struct ccic_dev *ccic_dev = ctrl->ccic_dev;

	if (en) {
		ret = pm_runtime_get_sync(&ccic_dev->pdev->dev);
		if (ret < 0) {
			pr_err("rpm get failed\n");
			return ret;
		}

		//clk_prepare_enable(ccic_dev->ahb_clk);
		reset_control_deassert(ccic_dev->ahb_reset);

		clk_prepare_enable(ccic_dev->clk4x);
		reset_control_deassert(ccic_dev->ccic_4x_reset);
		clk_prepare_enable(ccic_dev->csi_clk);
		reset_control_deassert(ccic_dev->csi_reset);

		ret = ccic_clk_set_rate(ctrl, SC2_MODE_ISP);
		if (ret < 0) {
			pm_runtime_put_sync(&ccic_dev->pdev->dev);
			return ret;
		}

	} else {
		clk_disable_unprepare(ccic_dev->csi_clk);
		reset_control_assert(ccic_dev->csi_reset);

		clk_disable_unprepare(ccic_dev->clk4x);
		reset_control_assert(ccic_dev->ccic_4x_reset);

		//clk_disable_unprepare(ccic_dev->ahb_clk);
		reset_control_assert(ccic_dev->ahb_reset);

		pm_runtime_put_sync(&ccic_dev->pdev->dev);
	}

	pr_debug("ccic%d clock %s", ccic_dev->index, en ? "enabled" : "disabled");

	return ret;
}

int ccic_config_csi2_mbus(struct ccic_ctrl *ctrl, int md, u8 vc0, u8 vc1, int lanes)
{
	int ret;
	struct ccic_dev *ccic_dev = ctrl->ccic_dev;
	struct mipi_csi2 csi2para;

	ret = ccic_config_csi2_vc(ctrl, md, vc0, vc1);
	if (ret)
		return ret;

	csi2para.calc_dphy = 0;
	csi2para.dphy[0] = 0x00000001;
	csi2para.dphy[1] = 0xa2848888;
	csi2para.dphy[2] = 0x0000201a;	//0x0000201a: 1.0G ~ 1.5G
	csi2para.dphy[3] = 0x000000ff;
	csi2para.dphy[4] = 0x1001;
	csi2para.dphy_desc.nr_lane = lanes;
	ret = ccic_config_csi2_dphy(ctrl, &csi2para, !!lanes);
	if (ret)
		return ret;

	pr_debug("ccic%d csi2 %s", ccic_dev->index, lanes ? "enabled" : "disabled");

	return ret;
}

int ccic_config_csi2idi_mux(struct ccic_ctrl *ctrl, int chnl, int idi, int en)
{
	struct ccic_dev *csi2idi = NULL;
	struct ccic_dev *tmp;
	int csi2idi_idx;

	if (idi == CCIC_CSI2IDI0) {
		csi2idi_idx = 0;
	} else if (idi == CCIC_CSI2IDI1) {
		csi2idi_idx = 2;
	} else {
		pr_err("%s: invalid idi index %d\n", __func__, idi);
		return -EINVAL;
	}

	list_for_each_entry(tmp, &ccic_devices, list) {
		if (tmp->index == csi2idi_idx) {
			csi2idi = tmp;
			break;
		}
	}

	if (!csi2idi) {
		pr_err("%s: ccic%d not found\n", __func__, csi2idi_idx);
		return -ENODEV;
	}

	if (!en) {
		ccic_csi2idi_reset(csi2idi, 1);
		return 0;
	}

	ccic_config_idi_sel(csi2idi->ctrl, CCIC_IDI_SEL_REPACK);
	if (ctrl->ccic_dev->index == csi2idi->index) {
		ccic_config_idi_mux(csi2idi->ctrl, CCIC_IDI_MUX_LOCAL_MAIN);
	} else if (ctrl->ccic_dev->index == 1) {
		if (chnl == CCIC_CSI2VC_MAIN)
			ccic_config_idi_mux(csi2idi->ctrl, CCIC_IDI_MUX_IPE2_MAIN);
		else
			ccic_config_idi_mux(csi2idi->ctrl, CCIC_IDI_MUX_IPE2_VCDT);
	} else {
		ccic_config_idi_mux(csi2idi->ctrl, CCIC_IDI_MUX_REMOTE_VCDT);
	}

	ccic_csi2idi_reset(csi2idi, 0);
	return 0;
}

int ccic_reset_csi2idi(struct ccic_ctrl *ctrl, int idi, int rst)
{
	struct ccic_dev *csi2idi = NULL;
	struct ccic_dev *tmp;
	int csi2idi_idx;

	if (idi == 0) {
		csi2idi_idx = 0;
	} else if (idi == 1) {
		csi2idi_idx = 2;
	} else {
		pr_err("%s: invalid idi index %d\n", __func__, idi);
		return -EINVAL;
	}

	list_for_each_entry(tmp, &ccic_devices, list) {
		if (tmp->index == csi2idi_idx) {
			csi2idi = tmp;
			break;
		}
	}

	if (!csi2idi) {
		pr_err("%s: ccic%d not found\n", __func__, csi2idi_idx);
		return -ENODEV;
	}

	ccic_csi2idi_reset(csi2idi, rst);
	return 0;
}

static struct ccic_ctrl_ops ccic_ctrl_ops = {
	.irq_mask = ccic_irqmask,
	.clk_enable = ccic_clk_enable,
	.config_csi2_mbus = ccic_config_csi2_mbus,
	.config_csi2idi_mux = ccic_config_csi2idi_mux,
	.reset_csi2idi = ccic_reset_csi2idi,
};

static int ccic_init_clk(struct ccic_dev *dev)
{
#ifdef CONFIG_ARCH_SPACEMIT
	dev->axi_clk = devm_clk_get(&dev->pdev->dev, "isp_axi");
	if (IS_ERR(dev->axi_clk))
		return PTR_ERR(dev->axi_clk);
/*
	dev->ahb_clk = devm_clk_get(&dev->pdev->dev, "isp_ahb");
	if (IS_ERR(dev->ahb_clk))
		return PTR_ERR(dev->ahb_clk);
*/
	dev->ahb_reset = devm_reset_control_get_optional_shared(&dev->pdev->dev, "isp_ahb_reset");
	if (IS_ERR_OR_NULL(dev->ahb_reset))
		return PTR_ERR(dev->ahb_reset);

	dev->csi_reset = devm_reset_control_get_optional_shared(&dev->pdev->dev, "csi_reset");
	if (IS_ERR_OR_NULL(dev->csi_reset))
		return PTR_ERR(dev->csi_reset);

	dev->ccic_4x_reset = devm_reset_control_get_optional_shared(&dev->pdev->dev, "ccic_4x_reset");
	if (IS_ERR_OR_NULL(dev->ccic_4x_reset))
		return PTR_ERR(dev->ccic_4x_reset);

	dev->isp_ci_reset = devm_reset_control_get_optional_shared(&dev->pdev->dev, "isp_ci_reset");
	if (IS_ERR_OR_NULL(dev->isp_ci_reset))
		return PTR_ERR(dev->isp_ci_reset);

	dev->csi_clk = devm_clk_get(&dev->pdev->dev, "csi_func");
	if (IS_ERR(dev->csi_clk))
		return PTR_ERR(dev->csi_clk);

	dev->clk4x = devm_clk_get(&dev->pdev->dev, "ccic_func");
	return PTR_ERR_OR_ZERO(dev->clk4x);
#else
	return 0;
#endif
}

static int ccic_device_register(struct ccic_dev *ccic_dev)
{
	struct ccic_dev *other;

	mutex_lock(&list_lock);
	list_for_each_entry(other, &ccic_devices, list) {
		if (other->index == ccic_dev->index) {
			dev_warn(ccic_dev->dev, "ccic%d already registered\n",
				 ccic_dev->index);
			mutex_unlock(&list_lock);
			return -EBUSY;
		}
	}

	list_add_tail(&ccic_dev->list, &ccic_devices);
	mutex_unlock(&list_lock);
	return 0;
}

static int ccic_device_unregister(struct ccic_dev *ccic_dev)
{
	mutex_lock(&list_lock);
	list_del(&ccic_dev->list);
	mutex_unlock(&list_lock);
	return 0;
}

int ccic_dphy_hssettle_set(unsigned int ccic_id, unsigned int dphy_freg)
{
	u32 reg_settle = 0x00002b00;
	struct ccic_dev *ccic_dev = NULL;
	struct ccic_dev *tmp;

	if (dphy_freg < 80)	//dphy_clock uint: MHZ
		return -EINVAL;
	// RX_Tsettle > TX_HSprepare; reg uint: (1 / (dphy_clock / 2))
	reg_settle =
	    (HS_PREP_ZERO_MIN + HS_PREP_MAX) * dphy_freg / (2 * 2 * 1000) + (6 / 2);
	reg_settle = reg_settle << CSI2_DPHY3_HS_SETTLE_SHIFT;

	mutex_lock(&list_lock);
	list_for_each_entry(tmp, &ccic_devices, list) {
		if (tmp->index == ccic_id) {
			ccic_dev = tmp;
			break;
		}
	}
	if (!ccic_dev) {
		pr_err("ccic%d not found", ccic_id);
		return -ENODEV;
	}
	ccic_reg_write(ccic_dev, REG_CSI2_DPHY3, reg_settle);
	mutex_unlock(&list_lock);

	return 0;
}

EXPORT_SYMBOL_GPL(ccic_dphy_hssettle_set);

int ccic_ctrl_get(struct ccic_ctrl **ctrl_host, int id,
		  irqreturn_t(*handler) (struct ccic_ctrl *, u32))
{
	struct ccic_dev *ccic_dev = NULL;
	struct ccic_dev *tmp;
	struct ccic_ctrl *ctrl = NULL;

	list_for_each_entry(tmp, &ccic_devices, list) {
		if (tmp->index == id) {
			ccic_dev = tmp;
			break;
		}
	}
	if (!ccic_dev) {
		pr_err("ccic%d not found", id);
		return -ENODEV;
	}

	ctrl = ccic_dev->ctrl;
	ctrl->handler = handler;
	*ctrl_host = ctrl;
	pr_debug("acquire ccic%d ctrl dev succeed\n", id);

	return 0;
}

EXPORT_SYMBOL(ccic_ctrl_get);

void ccic_ctrl_put(struct ccic_ctrl *ctrl)
{
	// TODO
}

EXPORT_SYMBOL(ccic_ctrl_put);

static void ipe_error_irq_handler(struct ccic_dev *ccic, u32 ipestatus, u32 csi2status)
{
	static DEFINE_RATELIMIT_STATE(rs, 5 * HZ, 20);

	if (__ratelimit(&rs)) {
		pr_err("CCIC%d: interrupt status 0x%08x, csi2 status 0x%08x\n",
		       ccic->index, ipestatus, csi2status);
	} else {
		/* aovid soft lockup due to high frequency interrupt */
		ccic_reg_clear_bit(ccic, REG_IRQMASK, CSI2PHYERRS);
		pr_err("CCIC%d: too many interrupt errors, mask\n", ccic->index);
	}
}

static irqreturn_t k1x_ccic_isr(int irq, void *data)
{
	struct ccic_dev *ccic_dev = data;
	uint32_t irqs, csi2status;

	irqs = ccic_reg_read(ccic_dev, REG_IRQSTAT);
	if (!(irqs & ~IRQ_IDI_PRO_LINE))
		return IRQ_NONE;

	csi2status = ccic_reg_read(ccic_dev, 0x108);
	ccic_reg_write(ccic_dev, REG_IRQSTAT, irqs & ~IRQ_IDI_PRO_LINE);

	if (irqs & CSI2PHYERRS)
		ipe_error_irq_handler(ccic_dev, irqs, csi2status);

	if (irqs & IRQ_DMA_PRO_LINE)
		pr_debug("CCIC%d: IRQ_DMA_PRO_LINE\n", ccic_dev->index);

	if (irqs & IRQ_IDI_PRO_LINE)
		pr_debug("CCIC%d: IRQ_IDI_PRO_LINE\n", ccic_dev->index);

	if (irqs & IRQ_CSI2IDI_FLUSH)
		pr_debug("CCIC%d: IRQ_CSI2IDI_FLUSH\n", ccic_dev->index);

	if (irqs & IRQ_CSI2IDI_HBLK2HSYNC)
		pr_debug("CCIC%d: IRQ_CSI2IDI_HBLK2HSYNC\n", ccic_dev->index);

	if (irqs & IRQ_DPHY_RX_CLKULPS_ACTIVE)
		pr_debug("CCIC%d: IRQ_DPHY_RX_CLKULPS_ACTIVE\n", ccic_dev->index);

	if (irqs & IRQ_DPHY_RX_CLKULPS)
		pr_debug("CCIC%d: IRQ_DPHY_RX_CLKULPS\n", ccic_dev->index);

	if (irqs & IRQ_DPHY_LN_ULPS_ACTIVE)
		pr_debug("CCIC%d: IRQ_DPHY_LN_ULPS_ACTIVE\n", ccic_dev->index);

	return IRQ_HANDLED;
}

static int k1x_ccic_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct ccic_dev *ccic_dev;
	struct ccic_ctrl *ccic_ctrl;
	struct ccic_dma *ccic_dma;
	struct device *dev = &pdev->dev;
	int ret;
	int irq;

	pr_debug("%s begin to probe\n", dev_name(&pdev->dev));

	ret = of_property_read_u32(np, "cell-index", &pdev->id);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to get alias id, errno %d\n", ret);
		return ret;
	}

	ccic_dev = devm_kzalloc(&pdev->dev, sizeof(*ccic_dev), GFP_KERNEL);
	if (!ccic_dev) {
		dev_err(&pdev->dev, "camera: Could not allocate ccic dev\n");
		return -ENOMEM;
	}

	ccic_ctrl = devm_kzalloc(&pdev->dev, sizeof(*ccic_ctrl), GFP_KERNEL);
	if (!ccic_ctrl) {
		dev_err(&pdev->dev, "camera: Could not allocate ctrl dev\n");
		return -ENOMEM;
	}

	ccic_dma = devm_kzalloc(&pdev->dev, sizeof(*ccic_dma), GFP_KERNEL);
	if (!ccic_dma) {
		dev_err(&pdev->dev, "camera: Could not allocate dma dev\n");
		return -ENOMEM;
	}

	/* get mem */
	ccic_dev->mem = platform_get_resource_byname(pdev, IORESOURCE_MEM, "ccic-regs");
	if (!ccic_dev->mem) {
		dev_err(&pdev->dev, "no mem resource");
		return -ENODEV;
	}
	ccic_dev->base = devm_ioremap(&pdev->dev, ccic_dev->mem->start,
				      resource_size(ccic_dev->mem));
	if (IS_ERR(ccic_dev->base)) {
		dev_err(&pdev->dev, "fail to remap iomem\n");
		return PTR_ERR(ccic_dev->base);
	}

	/* get irqs */
	irq = platform_get_irq_byname(pdev, "ipe-irq");
	if (irq < 0) {
		dev_err(&pdev->dev, "no irq resource");
		return -ENODEV;
	}
	dev_dbg(&pdev->dev, "ipe irq: %d\n", irq);
	ret = devm_request_irq(&pdev->dev, irq, k1x_ccic_isr,
			       IRQF_SHARED, K1X_CCIC_DRV_NAME, ccic_dev);
	if (ret) {
		dev_err(&pdev->dev, "fail to request irq\n");
		return ret;
	}

	/* ccic device and ctrl init */
	ccic_ctrl->ccic_dev = ccic_dev;
	ccic_ctrl->index = pdev->id;
	ccic_ctrl->ops = &ccic_ctrl_ops;
	atomic_set(&ccic_ctrl->usr_cnt, 0);

	ccic_dma->ccic_dev = ccic_dev;
	ccic_dma->ops = &ccic_dma_ops;

	ccic_dev->csiphy = csiphy_lookup_by_phandle(&pdev->dev, "spacemit,csiphy");
	if (!ccic_dev->csiphy) {
		dev_err(&pdev->dev, "fail to acquire csiphy\n");
		return -EPROBE_DEFER;
	}

	ccic_dev->index = pdev->id;
	ccic_dev->pdev = pdev;
	ccic_dev->dev = &pdev->dev;
	ccic_dev->ctrl = ccic_ctrl;
	ccic_dev->dma = ccic_dma;
	ccic_dev->interrupt_mask_value = CSI2PHYERRS;
	dev_set_drvdata(dev, ccic_dev);

	/* enable runtime pm */
	pm_runtime_enable(&pdev->dev);

	ccic_init_clk(ccic_dev);

	ccic_device_register(ccic_dev);

	pr_debug("%s probed", dev_name(&pdev->dev));

	return ret;
}

static int k1x_ccic_remove(struct platform_device *pdev)
{
	struct ccic_dev *ccic_dev;
	struct ccic_dma *dma;

	ccic_dev = dev_get_drvdata(&pdev->dev);
	dma = ccic_dev->dma;

	ccic_device_unregister(ccic_dev);

	/* disable runtime pm */
	pm_runtime_disable(&pdev->dev);

	return 0;
}

static const struct of_device_id k1x_ccic_dt_match[] = {
	{.compatible = "zynq,k1x-ccic",.data = NULL },
	{.compatible = "spacemit,k1xccic",.data = NULL },
	{ },
};

MODULE_DEVICE_TABLE(of, k1x_ccic_dt_match);

struct platform_driver k1x_ccic_driver = {
	.driver = {
		.name = K1X_CCIC_DRV_NAME,
		.of_match_table = of_match_ptr(k1x_ccic_dt_match),
	},
	.probe = k1x_ccic_probe,
	.remove = k1x_ccic_remove,
};

int __init k1x_ccic_driver_init(void)
{
	int ret;

	ret = ccic_csiphy_register();
	if (ret < 0)
		return ret;

	ret = platform_driver_register(&k1x_ccic_driver);
	if (ret < 0)
		ccic_csiphy_unregister();

	return ret;
}

void __exit k1x_ccic_driver_exit(void)
{
	platform_driver_unregister(&k1x_ccic_driver);
	ccic_csiphy_unregister();
}

module_init(k1x_ccic_driver_init);
module_exit(k1x_ccic_driver_exit);
/* module_platform_driver(k1x_ccic_driver); */

MODULE_DESCRIPTION("K1X CCIC Driver");
MODULE_LICENSE("GPL");
