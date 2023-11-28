// SPDX-License-Identifier: GPL-2.0-only
/*
 * Spacemit Generic power domain support.
 *
 * Copyright (c) 2023 SPACEMIT, Co. Ltd.
 */

#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/pm_clock.h>
#include <linux/pm_domain.h>
#include <linux/of_address.h>
#include <linux/of_clk.h>
#include <linux/of_platform.h>
#include <linux/clk.h>
#include <linux/regmap.h>
#include <linux/pm_qos.h>
#include <linux/mfd/syscon.h>
#include <linux/spinlock_types.h>
#include <linux/regulator/consumer.h>
#include "atomic_qos.h"

#define MAX_REGMAP		5
#define MAX_REGULATOR_PER_DOMAIN	5

#define MPMU_REGMAP_INDEX	0
#define APMU_REGMAP_INDEX	1

#define APMU_POWER_STATUS_REG	0xf0
#define MPMU_APCR_PER_REG	0x1098

#define PM_QOS_BLOCK_C1		0x0 /* core wfi */
#define PM_QOS_BLOCK_C2		0x2 /* core power off */
#define PM_QOS_BLOCK_M2		0x6 /* core l2 off */
#define PM_QOS_BLOCK_AXI        0x7 /* d1p */
#define PM_QOS_BLOCK_DDR        12 /* d1 */
#define PM_QOS_BLOCK_UDR_VCTCXO 13 /* d2 */
#define PM_QOS_BLOCK_UDR        14 /* d2pp */
#define PM_QOS_BLOCK_DEFAULT_VALUE	15

#define PM_QOS_AXISDD_OFFSET	31
#define PM_QOS_DDRCORSD_OFFSET	27
#define PM_QOS_APBSD_OFFSET	26
#define PM_QOS_VCTCXOSD_OFFSET	19
#define PM_QOS_STBYEN_OFFSET	13
#define PM_QOS_PE_VOTE_AP_SLPEN_OFFSET	3

struct spacemit_pm_domain_param {
	int reg_pwr_ctrl;
	int pm_qos;
	int bit_hw_mode;
	int bit_sleep2;
	int bit_sleep1;
	int bit_isolation;
	int bit_auto_pwr_on;
	int bit_hw_pwr_stat;
	int bit_pwr_stat;
	int use_hw;
};

struct spacemit_pm_domain {
	struct generic_pm_domain genpd;
	int pm_index;
	struct device *gdev;
	struct notifier_block notifier;
	struct atomic_freq_qos_request qos;
	struct spacemit_pm_domain_param param;
	struct regulator *rgr[MAX_REGULATOR_PER_DOMAIN];
	int rgr_count;
};

struct spacemit_pmu {
	struct device *dev;
	int number_domains;
	struct genpd_onecell_data genpd_data;
	struct regmap *regmap[MAX_REGMAP];
	struct spacemit_pm_domain **domains;
};

static DEFINE_SPINLOCK(spacemit_apcr_qos_lock);

static struct spacemit_pmu *gpmu;

static struct atomic_freq_constraints afreq_constraints;

static const struct of_device_id spacemit_regmap_dt_match[] = {
	{ .compatible = "spacemit,spacemit-mpmu", },
	{ .compatible = "spacemit,spacemit-apmu", },
};

static int spacemit_pd_power_off(struct generic_pm_domain *domain)
{
	unsigned int val;
	int loop, ret;

	struct spacemit_pm_domain *spd = container_of(domain, struct spacemit_pm_domain, genpd);

	if (spd->param.reg_pwr_ctrl == 0) {
		/* close the power */
		for (loop = 0; loop < spd->rgr_count; ++loop) {
			ret = regulator_disable(spd->rgr[loop]);
			if (ret < 0) {
				pr_err("%s: regulator disable failed\n", __func__);
				return ret;
			}
		}

		return 0;
	}

	if (!spd->param.use_hw) {
		/* this is the sw type */
		regmap_read(gpmu->regmap[APMU_REGMAP_INDEX], spd->param.reg_pwr_ctrl, &val);
		val &= ~(1 << spd->param.bit_isolation);
		regmap_write(gpmu->regmap[APMU_REGMAP_INDEX], spd->param.reg_pwr_ctrl, val);

		usleep_range(10, 15);

		/* mcu power off */
		regmap_read(gpmu->regmap[APMU_REGMAP_INDEX], spd->param.reg_pwr_ctrl, &val);
		val &= ~((1 << spd->param.bit_sleep1) | (1 << spd->param.bit_sleep2));
		regmap_write(gpmu->regmap[APMU_REGMAP_INDEX], spd->param.reg_pwr_ctrl, val);

		usleep_range(10, 15);

		for (loop = 10000; loop >= 0; --loop) {
			regmap_read(gpmu->regmap[APMU_REGMAP_INDEX], APMU_POWER_STATUS_REG, &val);
			if ((val & (1 << spd->param.bit_pwr_stat)) == 0)
				break;
			usleep_range(4, 6);
		}
	} else {
		/* LCD */
		regmap_read(gpmu->regmap[APMU_REGMAP_INDEX], spd->param.reg_pwr_ctrl, &val);
		val &= ~(1 << spd->param.bit_auto_pwr_on);
		val &= ~(1 << spd->param.bit_hw_mode);
		regmap_write(gpmu->regmap[APMU_REGMAP_INDEX], spd->param.reg_pwr_ctrl, val);

		usleep_range(10, 30);

		for (loop = 10000; loop >= 0; --loop) {
			regmap_read(gpmu->regmap[APMU_REGMAP_INDEX], APMU_POWER_STATUS_REG, &val);
			if ((val & (1 << spd->param.bit_hw_pwr_stat)) == 0)
				break;
			usleep_range(4, 6);
		}
	}

	if (loop < 0) {
		pr_err("power-off domain: %d, error\n", spd->pm_index);
		return -EBUSY;
	}

	/* close the power */
	for (loop = 0; loop < spd->rgr_count; ++loop) {
		ret = regulator_disable(spd->rgr[loop]);
		if (ret < 0) {
			pr_err("%s: regulator disable failed\n", __func__);
			return ret;
		}
	}

	return 0;
}

static int spacemit_pd_power_on(struct generic_pm_domain *domain)
{
	int loop, ret;
	unsigned int val;

	struct spacemit_pm_domain *spd = container_of(domain, struct spacemit_pm_domain, genpd);

	/* enable the power */
	for (loop = 0; loop < spd->rgr_count; ++loop) {
		ret = regulator_enable(spd->rgr[loop]);
		if (ret < 0) {
			pr_err("%s: regulator disable failed\n", __func__);
			return ret;
		}
	}

	if (spd->param.reg_pwr_ctrl == 0)
		return 0;

	regmap_read(gpmu->regmap[APMU_REGMAP_INDEX], APMU_POWER_STATUS_REG, &val);
	if (val & (1 << spd->param.bit_pwr_stat))
		return 0;

	if (!spd->param.use_hw) {
		/* mcu power on */
		regmap_read(gpmu->regmap[APMU_REGMAP_INDEX], spd->param.reg_pwr_ctrl, &val);
		val |= (1 << spd->param.bit_sleep1);
		regmap_write(gpmu->regmap[APMU_REGMAP_INDEX], spd->param.reg_pwr_ctrl, val);

		usleep_range(20, 25);

		regmap_read(gpmu->regmap[APMU_REGMAP_INDEX], spd->param.reg_pwr_ctrl, &val);
		val |= (1 << spd->param.bit_sleep2) | (1 << spd->param.bit_sleep1);
		regmap_write(gpmu->regmap[APMU_REGMAP_INDEX], spd->param.reg_pwr_ctrl, val);

		usleep_range(20, 25);

		/* disable isolation */
		regmap_read(gpmu->regmap[APMU_REGMAP_INDEX], spd->param.reg_pwr_ctrl, &val);
		val |= (1 << spd->param.bit_isolation);
		regmap_write(gpmu->regmap[APMU_REGMAP_INDEX], spd->param.reg_pwr_ctrl, val);

		usleep_range(10, 15);

		for (loop = 10000; loop >= 0; --loop) {
			regmap_read(gpmu->regmap[APMU_REGMAP_INDEX], APMU_POWER_STATUS_REG, &val);
			if (val & (1 << spd->param.bit_pwr_stat))
				break;
			usleep_range(4, 6);
		}
	} else {
		/* LCD */
		regmap_read(gpmu->regmap[APMU_REGMAP_INDEX], spd->param.reg_pwr_ctrl, &val);
		val |= (1 << spd->param.bit_auto_pwr_on);
		val |= (1 << spd->param.bit_hw_mode);
		regmap_write(gpmu->regmap[APMU_REGMAP_INDEX], spd->param.reg_pwr_ctrl, val);
	
		usleep_range(290, 310);
		
		for (loop = 10000; loop >= 0; --loop) {
			regmap_read(gpmu->regmap[APMU_REGMAP_INDEX], APMU_POWER_STATUS_REG, &val);
			if (val & (1 << spd->param.bit_hw_pwr_stat))
				break;
			usleep_range(4, 6);
		}
	}

	if (loop < 0) {
		pr_err("power-off domain: %d, error\n", spd->pm_index);
		return -EBUSY;
	}

	return 0;
}

static int spacemit_pd_attach_dev(struct generic_pm_domain *genpd, struct device *dev)
{
	int err, i = 0, count;
	struct clk *clk;
	static int atomic_freq_constraints_init_flag;
	const char *strings[MAX_REGULATOR_PER_DOMAIN];
	struct spacemit_pm_domain *spd = container_of(genpd, struct spacemit_pm_domain, genpd);

	if (!of_property_read_bool(dev->of_node, "clk,pm-runtime,no-sleep")) {
		err = pm_clk_create(dev);
		if (err) {
			 dev_err(dev, "pm_clk_create failed %d\n", err);
			 return err;
		}

		while ((clk = of_clk_get(dev->of_node, i++)) && !IS_ERR(clk)) {
			err = pm_clk_add_clk(dev, clk);
			if (err) {
				 dev_err(dev, "pm_clk_add_clk failed %d\n", err);
				 clk_put(clk);
				 pm_clk_destroy(dev);
				 return err;
			}
		}
	}

	/* parse the regulator */
	if (!of_property_read_bool(dev->of_node, "regulator,pm-runtime,no-sleep")) {
		count = of_property_count_strings(dev->of_node, "vin-supply-names");
		if (count < 0)
			pr_info("no vin-suppuly-names found\n");
		else {
			err = of_property_read_string_array(dev->of_node, "vin-supply-names",
				strings, count);
			if (err < 0) {
				pr_info("read string array vin-supplu-names error\n");
				return err;
			}

			for (i = 0; i < count; ++i) {
				spd->rgr[i] = devm_regulator_get(dev, strings[i]);
				if (IS_ERR(spd->rgr[i])) {
					pr_err("regulator supply %s, get failed\n", strings[i]);
					return PTR_ERR(spd->rgr[i]);
				}
			}

			spd->rgr_count = count;
		}
	}
	/* add one notifier is ok */
	if (!atomic_freq_constraints_init_flag) {
		atomic_freq_constraints_init(&afreq_constraints);
		err = atomic_freq_qos_add_notifier(&afreq_constraints, FREQ_QOS_MAX, &spd->notifier);
		if (err)
			return err;

		atomic_freq_constraints_init_flag = 1;
	}

	atomic_freq_qos_add_request(&afreq_constraints, &spd->qos, FREQ_QOS_MAX, PM_QOS_BLOCK_DEFAULT_VALUE);

	return 0;
}

static void spacemit_pd_detach_dev(struct generic_pm_domain *genpd, struct device *dev)
{
	int i = 0;
	struct spacemit_pm_domain *spd = container_of(genpd, struct spacemit_pm_domain, genpd);

	if (!of_property_read_bool(dev->of_node, "clk,pm-runtime,no-sleep"))
		pm_clk_destroy(dev);

	atomic_freq_qos_remove_notifier(&afreq_constraints, FREQ_QOS_MAX, &spd->notifier);

	/* */
	for (i = 0; i < spd->rgr_count; ++i)
		devm_regulator_put(spd->rgr[i]);
}

static int spacemit_pm_notfier_call(struct notifier_block *nb, unsigned long action, void *data)
{
	unsigned int apcr_per;
	unsigned int apcr_clear = 0, apcr_set = (1 << PM_QOS_PE_VOTE_AP_SLPEN_OFFSET);

	spin_lock(&spacemit_apcr_qos_lock);

	switch (action) {
		case PM_QOS_BLOCK_C1:
		case PM_QOS_BLOCK_C2:
		case PM_QOS_BLOCK_M2:
		case PM_QOS_BLOCK_AXI:
			apcr_clear |= (1 << PM_QOS_AXISDD_OFFSET);
			apcr_clear |= (1 << PM_QOS_DDRCORSD_OFFSET);
			apcr_clear |= (1 << PM_QOS_APBSD_OFFSET);
			apcr_clear |= (1 << PM_QOS_VCTCXOSD_OFFSET);
			apcr_clear |= (1 << PM_QOS_STBYEN_OFFSET);
			break;
		case PM_QOS_BLOCK_DDR:
			apcr_set |= (1 << PM_QOS_AXISDD_OFFSET);
			apcr_clear |= (1 << PM_QOS_DDRCORSD_OFFSET);
			apcr_clear |= (1 << PM_QOS_APBSD_OFFSET);
			apcr_clear |= (1 << PM_QOS_VCTCXOSD_OFFSET);
			apcr_clear |= (1 << PM_QOS_STBYEN_OFFSET);
			break;
		case PM_QOS_BLOCK_UDR:
			apcr_clear |= (1 << PM_QOS_STBYEN_OFFSET);
			apcr_set |= (1 << PM_QOS_AXISDD_OFFSET);
			apcr_set |= (1 << PM_QOS_DDRCORSD_OFFSET);
			apcr_set |= (1 << PM_QOS_APBSD_OFFSET);
			apcr_set |= (1 << PM_QOS_VCTCXOSD_OFFSET);
			break;
		case PM_QOS_BLOCK_DEFAULT_VALUE:
			apcr_set |= (1 << PM_QOS_AXISDD_OFFSET);
			apcr_set |= (1 << PM_QOS_DDRCORSD_OFFSET);
			apcr_set |= (1 << PM_QOS_APBSD_OFFSET);
			apcr_set |= (1 << PM_QOS_VCTCXOSD_OFFSET);
			apcr_set |= (1 << PM_QOS_STBYEN_OFFSET);
			break;
		default:
			pr_warn("Invalidate pm qos value\n");
			spin_unlock(&spacemit_apcr_qos_lock);
	}

	regmap_read(gpmu->regmap[MPMU_REGMAP_INDEX], MPMU_APCR_PER_REG, &apcr_per);
	apcr_per &= ~(apcr_clear);
	apcr_per |= apcr_set;
	regmap_write(gpmu->regmap[MPMU_REGMAP_INDEX], MPMU_APCR_PER_REG, apcr_per);

	regmap_read(gpmu->regmap[MPMU_REGMAP_INDEX], MPMU_APCR_PER_REG, &apcr_per);

	spin_unlock(&spacemit_apcr_qos_lock);

	return 0;
}

static int spacemit_genpd_stop(struct device *dev)
{
	/* do the clk & pm_qos related things */
	struct generic_pm_domain *pd = pd_to_genpd(dev->pm_domain);
	struct spacemit_pm_domain *spd = container_of(pd, struct spacemit_pm_domain, genpd);

	/* dealing with the pm_qos */
	atomic_freq_qos_update_request(&spd->qos, PM_QOS_BLOCK_DEFAULT_VALUE);

	/* disable the clk */
	pm_clk_suspend(dev);

	return 0;
}

static int spacemit_genpd_start(struct device *dev)
{
	/* do the clk & pm_qos related things */
	struct generic_pm_domain *pd = pd_to_genpd(dev->pm_domain);
	struct spacemit_pm_domain *spd = container_of(pd, struct spacemit_pm_domain, genpd);

	/* enable the clk */
	pm_clk_resume(dev);

	/* dealing with the pm_qos */
	atomic_freq_qos_update_request(&spd->qos, spd->param.pm_qos);
	
	return 0;
}

static int spacemit_get_pm_domain_parameters(struct device_node *node, struct spacemit_pm_domain *pd)
{
	int err;

	err = of_property_read_u32(node, "pm_qos", &pd->param.pm_qos);
	if (err) {
		pr_err("%s:%d, failed to retrive the domain pm_qos\n",
				__func__, __LINE__);
		return -EINVAL;
	}

	err = of_property_read_u32(node, "reg_pwr_ctrl", &pd->param.reg_pwr_ctrl);
	err |= of_property_read_u32(node, "bit_hw_mode", &pd->param.bit_hw_mode);
	err |= of_property_read_u32(node, "bit_sleep2", &pd->param.bit_sleep2);
	err |= of_property_read_u32(node, "bit_sleep1", &pd->param.bit_sleep1);
	err |= of_property_read_u32(node, "bit_isolation", &pd->param.bit_isolation);
	err |= of_property_read_u32(node, "bit_auto_pwr_on", &pd->param.bit_auto_pwr_on);
	err |= of_property_read_u32(node, "bit_hw_pwr_stat", &pd->param.bit_hw_pwr_stat);
	err |= of_property_read_u32(node, "bit_pwr_stat", &pd->param.bit_pwr_stat);
	err |= of_property_read_u32(node, "use_hw", &pd->param.use_hw);

	if (err)
		pr_warn("get pm domain parameter failed\n");

	return 0;
}

static int spacemit_pm_add_one_domain(struct spacemit_pmu *pmu, struct device_node *node)
{
	int err;
	int id;
	struct spacemit_pm_domain *pd;

	err = of_property_read_u32(node, "reg", &id);
	if (err) {
		pr_err("%s:%d, failed to retrive the domain id\n",
				__func__, __LINE__);
		return -EINVAL;
	}

	if (id >= pmu->number_domains) {
		pr_err("%pOFn: invalid domain id %d\n", node, id);
		return -EINVAL;
	}

	pd = (struct spacemit_pm_domain *)devm_kzalloc(pmu->dev, sizeof(struct spacemit_pm_domain), GFP_KERNEL);
	if (!pd)
		return -ENOMEM;

	pd->pm_index = id;

	/* we will add all the notifiers to this device */
	pd->gdev = pmu->dev;

	err = spacemit_get_pm_domain_parameters(node, pd);
	if (err)
		return -EINVAL;

	pd->notifier.notifier_call = spacemit_pm_notfier_call,

	pd->genpd.name = kbasename(node->full_name);
	pd->genpd.power_off = spacemit_pd_power_off;
	pd->genpd.power_on = spacemit_pd_power_on;
	pd->genpd.attach_dev = spacemit_pd_attach_dev;
	pd->genpd.detach_dev = spacemit_pd_detach_dev;

	pd->genpd.dev_ops.stop = spacemit_genpd_stop;
	pd->genpd.dev_ops.start = spacemit_genpd_start;

	pm_genpd_init(&pd->genpd, NULL, true);

	pmu->domains[id] = pd;

	return 0;
}

static void spacemit_pm_remove_one_domain(struct spacemit_pm_domain *pd)
{
	int ret;

	ret = pm_genpd_remove(&pd->genpd);
	if (ret < 0) {
		pr_err("failed to remove domain '%s' : %d\n", pd->genpd.name, ret);
	}

	/* devm will free our memory */
}

static int spacemit_pm_add_subdomain(struct spacemit_pmu *pmu, struct device_node *parent)
{
	struct device_node *np;
	struct generic_pm_domain *child_domain, *parent_domain;
	int err, idx;

	for_each_child_of_node(parent, np) {
		err = of_property_read_u32(parent, "reg", &idx);
		if (err) {
			pr_err("%pOFn: failed to retrive domain id (reg): %d\n",
					parent, err);
			goto err_out;
		}

		parent_domain = &pmu->domains[idx]->genpd;

		err = spacemit_pm_add_one_domain(pmu, np);
		if (err) {
			pr_err("failed to handle node %pOFn: %d\n", np, err);
			goto err_out;
		}

		err = of_property_read_u32(np, "reg", &idx);
		if (err) {
			pr_err("%pOFn: failed to retrive domain id (reg): %d\n",
					parent, err);
			goto err_out;
		}

		child_domain = &pmu->domains[idx]->genpd;

		err = pm_genpd_add_subdomain(parent_domain, child_domain);
		if (err) {
			pr_err("%s failed to add subdomain %s: %d\n",
					parent_domain->name, child_domain->name, err);
			goto err_out;
		} else {
			pr_info("%s add subdomain: %s\n",
					parent_domain->name, child_domain->name);
		}

		spacemit_pm_add_subdomain(pmu, np);
	}

	return 0;

err_out:
	of_node_put(np);
	return err;
}

static void spacemit_pm_domain_cleanup(struct spacemit_pmu *pmu)
{
	struct spacemit_pm_domain *pd;
	int i;

	for (i = 0; i < pmu->number_domains; i++) {
		pd = pmu->domains[i];
		if (pd)
			spacemit_pm_remove_one_domain(pd);
	}

	/* devm will free our memory */

}

static int spacemit_pm_domain_probe(struct platform_device *pdev)
{
	int err = 0, i;
	struct device *dev = &pdev->dev;
	struct device_node *node;
	struct device_node *np = dev->of_node;
	struct spacemit_pmu *pmu = NULL;

	pmu = (struct spacemit_pmu *)devm_kzalloc(dev, sizeof(struct spacemit_pmu), GFP_KERNEL);
	if (pmu == NULL) {
		pr_err("%s:%d, err\n", __func__, __LINE__);
		return -ENOMEM;
	}

	pmu->dev = dev;

	for (i = 0; i < sizeof(spacemit_regmap_dt_match) / sizeof(spacemit_regmap_dt_match[0]); ++i) {
		pmu->regmap[i] = syscon_regmap_lookup_by_compatible(spacemit_regmap_dt_match[i].compatible);
		if (IS_ERR(pmu->regmap[i])) {
			pr_err("%s:%d err\n", __func__, __LINE__);
			return PTR_ERR(pmu->regmap[i]);
		}
	}

	/* get number power domains */
	err = of_property_read_u32(np, "domains", &pmu->number_domains);
	if (err) {
		pr_err("%s:%d, failed to retrive the number of domains\n",
				__func__, __LINE__);
		return -EINVAL;
	}

	pmu->domains = devm_kzalloc(dev, sizeof(struct spacemit_pm_domain *) * pmu->number_domains,
			GFP_KERNEL);
	if (!pmu->domains) {
		pr_err("%s:%d, err\n", __func__, __LINE__);
		return -ENOMEM;
	}

	err = -ENODEV;

	for_each_available_child_of_node(np, node) {
		err = spacemit_pm_add_one_domain(pmu, node);
		if (err) {
			pr_err("%s:%d, failed to handle node %pOFn: %d\n", __func__, __LINE__,
					node, err);
			of_node_put(node);
			goto err_out;
		}

		err = spacemit_pm_add_subdomain(pmu, node);
		if (err) {
			pr_err("%s:%d, failed to handle subdomain node %pOFn: %d\n",
					__func__, __LINE__, node, err);
			of_node_put(node);
			goto err_out;
		}
	}

	if(err) {
		pr_err("no power domains defined\n");
		goto err_out;
	}

	pmu->genpd_data.domains = (struct generic_pm_domain **)pmu->domains;
	pmu->genpd_data.num_domains = pmu->number_domains;

	err = of_genpd_add_provider_onecell(np, &pmu->genpd_data);
	if (err) {
		pr_err("failed to add provider: %d\n", err);
		goto err_out;
	}

	gpmu = pmu;

	return 0;

err_out:
	spacemit_pm_domain_cleanup(pmu);
	return err;
}

static const struct of_device_id spacemit_pm_domain_dt_match[] = {
	{ .compatible = "spacemit,power-controller", },
	{ },
};

static struct platform_driver spacemit_pm_domain_driver = {
	.probe = spacemit_pm_domain_probe,
	.driver = {
		.name   = "spacemit-pm-domain",
		.of_match_table = spacemit_pm_domain_dt_match,
	},
};

static int __init spacemit_pm_domain_drv_register(void)
{
	return platform_driver_register(&spacemit_pm_domain_driver);
}
core_initcall(spacemit_pm_domain_drv_register);
