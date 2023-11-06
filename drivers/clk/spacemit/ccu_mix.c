// SPDX-License-Identifier: GPL-2.0-only
/*
 * Spacemit clock type mix(div/mux/gate/factor)
 *
 * Copyright (c) 2023, spacemit Corporation.
 *
 */
#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/err.h>

#include "ccu_mix.h"

#define TIMEOUT_LIMIT (20000) /* max timeout 10000us */

static void ccu_mix_disable(struct clk_hw *hw)
{
	struct ccu_mix *mix = hw_to_ccu_mix(hw);
	struct ccu_common * common = &mix->common;
	struct ccu_gate_config *gate = mix->gate;
	unsigned long flags = 0;
	unsigned long rate;
	u32 tmp;

	if (!gate)
		return;
	if (common->lock)
		spin_lock_irqsave(common->lock, flags);

	if (common->reg_type == CLK_DIV_TYPE_1REG_NOFC_V1
		|| common->reg_type == CLK_DIV_TYPE_1REG_FC_V2)
		tmp = readl(common->base + common->reg_ctrl);
	else
		tmp = readl(common->base + common->reg_sel);

	tmp &= ~gate->gate_mask;
	tmp |= gate->val_disable;

	if (common->reg_type == CLK_DIV_TYPE_1REG_NOFC_V1
		|| common->reg_type == CLK_DIV_TYPE_1REG_FC_V2)
		writel(tmp, common->base + common->reg_ctrl);
	else
		writel(tmp, common->base + common->reg_sel);

	if (gate->flags & SPACEMIT_CLK_GATE_NEED_DELAY) {
		rate = clk_hw_get_rate(&common->hw);

		if (rate == 0)
			pr_err("clock rate of %s is 0.\n", clk_hw_get_name(&common->hw));
		else
			/* Need delay 2M cycles. */
			udelay(DIV_ROUND_UP(2000000, rate));
	}

	if (common->lock)
		spin_unlock_irqrestore(common->lock, flags);

	return;
}

static int ccu_mix_enable(struct clk_hw *hw)
{
	struct ccu_mix *mix = hw_to_ccu_mix(hw);
	struct ccu_common * common = &mix->common;
	struct ccu_gate_config *gate = mix->gate;
	unsigned long flags = 0;
	unsigned long rate;
	u32 tmp;
	u32 val = 0;
	int timeout_power = 1;

    if (!gate)
		return 0;
	if (common->lock)
		spin_lock_irqsave(common->lock, flags);

	if (common->reg_type == CLK_DIV_TYPE_1REG_NOFC_V1
		|| common->reg_type == CLK_DIV_TYPE_1REG_FC_V2)
		tmp = readl(common->base + common->reg_ctrl);
	else
		tmp = readl(common->base + common->reg_sel);

	tmp &= ~gate->gate_mask;
	tmp |= gate->val_enable;

	if (common->reg_type == CLK_DIV_TYPE_1REG_NOFC_V1
		|| common->reg_type == CLK_DIV_TYPE_1REG_FC_V2)
		writel(tmp, common->base + common->reg_ctrl);
	else
		writel(tmp, common->base + common->reg_sel);

	if (common->reg_type == CLK_DIV_TYPE_1REG_NOFC_V1
		|| common->reg_type == CLK_DIV_TYPE_1REG_FC_V2)
		val = readl(common->base + common->reg_ctrl);
	else
		val = readl(common->base + common->reg_sel);

	while ((val & gate->gate_mask) != gate->val_enable && (timeout_power < TIMEOUT_LIMIT)) {
		udelay(timeout_power);
		if (common->reg_type == CLK_DIV_TYPE_1REG_NOFC_V1
			|| common->reg_type == CLK_DIV_TYPE_1REG_FC_V2)
			val = readl(common->base + common->reg_ctrl);
		else
			val = readl(common->base + common->reg_sel);
		timeout_power *= 10;
	}

	if (timeout_power > 1) {
		if (val == tmp)
			pr_err("write clk_gate %s timeout occur, read pass after %d us delay\n",
			clk_hw_get_name(&common->hw), timeout_power);
		else
			pr_err("write clk_gate  %s timeout after %d us!\n", clk_hw_get_name(&common->hw), timeout_power);
	}

	if (common->lock)
		spin_unlock_irqrestore(common->lock, flags);

	if (gate->flags & SPACEMIT_CLK_GATE_NEED_DELAY) {
		rate = clk_hw_get_rate(&common->hw);

		if (rate == 0)
			pr_err("clock rate of %s is 0.\n", clk_hw_get_name(&common->hw));
		else
			/* Need delay 2M cycles. */
			udelay(DIV_ROUND_UP(2000000, rate));
	}

	return 0;
}

static int ccu_mix_is_enabled(struct clk_hw *hw)
{
	struct ccu_mix *mix = hw_to_ccu_mix(hw);
	struct ccu_common * common = &mix->common;
	struct ccu_gate_config *gate = mix->gate;
	unsigned long flags = 0;
	u32 tmp;

	if (!gate)
		return 1;

	if (common->lock)
		spin_lock_irqsave(common->lock, flags);

	if (common->reg_type == CLK_DIV_TYPE_1REG_NOFC_V1
		|| common->reg_type == CLK_DIV_TYPE_1REG_FC_V2)
		tmp = readl(common->base + common->reg_ctrl);
	else
		tmp = readl(common->base + common->reg_sel);

	if (common->lock)
		spin_unlock_irqrestore(common->lock, flags);

	return (tmp & gate->gate_mask) == gate->val_enable;
}

static unsigned long ccu_mix_recalc_rate(struct clk_hw *hw,
					unsigned long parent_rate)
{
	struct ccu_mix *mix = hw_to_ccu_mix(hw);
	struct ccu_common * common = &mix->common;
	struct ccu_div_config *div = mix->div;
	unsigned long val;
	u32 reg;

	if (!div){
		if (mix->factor)
			return parent_rate * mix->factor->mul / mix->factor->div;
		else
		    return parent_rate;
	}
    if (common->reg_type == CLK_DIV_TYPE_1REG_NOFC_V1
		|| common->reg_type == CLK_DIV_TYPE_1REG_FC_V2)
		reg = readl(common->base + common->reg_ctrl);
	else
		reg = readl(common->base + common->reg_sel);

	val = reg >> div->shift;
	val &= (1 << div->width) - 1;

	val = divider_recalc_rate(hw, parent_rate, val, div->table,
				  div->flags, div->width);

	return val;
}

static long ccu_mix_round_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long *prate)
{
	struct ccu_mix *mix = hw_to_ccu_mix(hw);
	struct ccu_div_config *div = mix->div;
    if (!div)
		return 0;
	return divider_round_rate(hw, rate, prate, div->table,
				  div->width, 0);
}

static int ccu_mix_set_rate(struct clk_hw *hw, unsigned long rate,
			   unsigned long parent_rate)
{
	struct ccu_mix *mix = hw_to_ccu_mix(hw);
	struct ccu_common * common = &mix->common;
	struct ccu_div_config *div = mix->div;
	unsigned long flags;
	unsigned long val;
	u32 reg;
	int ret, timeout = 50;

    if (!div)
		return 0;
	val = divider_get_val(rate, parent_rate, div->table, div->width,
			      div->flags);

	spin_lock_irqsave(common->lock, flags);

    if (common->reg_type == CLK_DIV_TYPE_1REG_NOFC_V1
		|| common->reg_type == CLK_DIV_TYPE_1REG_FC_V2)
		reg = readl(common->base + common->reg_ctrl);
	else
		reg = readl(common->base + common->reg_sel);

	reg &= ~GENMASK(div->width + div->shift - 1, div->shift);

    if (common->reg_type == CLK_DIV_TYPE_1REG_NOFC_V1
		|| common->reg_type == CLK_DIV_TYPE_1REG_FC_V2)
		writel(reg | (val << div->shift),
	       common->base + common->reg_ctrl);
	else
		writel(reg | (val << div->shift),
	       common->base + common->reg_sel);

   if (common->reg_type == CLK_DIV_TYPE_1REG_FC_V2
		   || common->reg_type == CLK_DIV_TYPE_2REG_FC_V4) {
		   timeout = 50;
		   val = readl(common->base + common->reg_ctrl);
		   val |= common->fc;
		   writel(val, common->base + common->reg_ctrl);

		   do {
			   val = readl(common->base + common->reg_ctrl);
			   timeout--;
			   if (!(val & (common->fc)))
				   break;
		   } while (timeout);

		   if (timeout == 0) {
		   	   printk("%s of %s timeout\n", __func__, clk_hw_get_name(&common->hw));
			   timeout = 5000;
			   do {
				   val = readl(common->base + common->reg_ctrl);
				   timeout--;
				   if (!(val & (common->fc)))
					   break;
			   } while (timeout);
			   if (timeout != 0) {
			   	   ret = 0;

			   } else {
				   ret = -EBUSY;
				   goto error;
			   }
		   }
	   }

error:
	spin_unlock_irqrestore(common->lock, flags);

	return ret;
}

static u8 ccu_mix_get_parent(struct clk_hw *hw)
{
	struct ccu_mix *mix = hw_to_ccu_mix(hw);
	struct ccu_common * common = &mix->common;
	struct ccu_mux_config *mux = mix->mux;
	u32 reg;
	u8 parent;

	if(!mux)
		return 0;

	if (common->reg_type == CLK_DIV_TYPE_1REG_NOFC_V1
		|| common->reg_type == CLK_DIV_TYPE_1REG_FC_V2)
		reg = readl(common->base + common->reg_ctrl);
	else
		reg = readl(common->base + common->reg_sel);

	parent = reg >> mux->shift;
	parent &= (1 << mux->width) - 1;

	if (mux->table) {
		int num_parents = clk_hw_get_num_parents(&common->hw);
		int i;

		for (i = 0; i < num_parents; i++)
			if (mux->table[i] == parent)
				return i;
	}
	return parent;
}

static int ccu_mix_set_parent(struct clk_hw *hw, u8 index)
{
	struct ccu_mix *mix = hw_to_ccu_mix(hw);
	struct ccu_common * common = &mix->common;
	struct ccu_mux_config *mux = mix->mux;
	unsigned long flags;
	u32 reg;

	if (mux->table)
		index = mux->table[index];

	spin_lock_irqsave(common->lock, flags);

	if (common->reg_type == CLK_DIV_TYPE_1REG_NOFC_V1
		|| common->reg_type == CLK_DIV_TYPE_1REG_FC_V2)
		reg = readl(common->base + common->reg_ctrl);
	else
		reg = readl(common->base + common->reg_sel);

	reg &= ~GENMASK(mux->width + mux->shift - 1, mux->shift);

	if (common->reg_type == CLK_DIV_TYPE_1REG_NOFC_V1
	|| common->reg_type == CLK_DIV_TYPE_1REG_FC_V2)
		writel(reg | (index << mux->shift), common->base + common->reg_ctrl);
	else
		writel(reg | (index << mux->shift), common->base + common->reg_sel);

	spin_unlock_irqrestore(common->lock, flags);

	return 0;
}

const struct clk_ops ccu_mix_ops = {
	.disable	 = ccu_mix_disable,
	.enable		 = ccu_mix_enable,
	.is_enabled	 = ccu_mix_is_enabled,
	.get_parent	 = ccu_mix_get_parent,
	.set_parent	 = ccu_mix_set_parent,
	.round_rate  = ccu_mix_round_rate,
	.recalc_rate = ccu_mix_recalc_rate,
	.set_rate	 = ccu_mix_set_rate,
};

