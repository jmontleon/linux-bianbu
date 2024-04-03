// SPDX-License-Identifier: GPL-2.0-only

#include <linux/cpufreq.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/cpumask.h>
#include <linux/clk/clk-conf.h>
#include <linux/pm_qos.h>
#include <linux/notifier.h>
#include <linux/regulator/consumer.h>
#include <linux/mutex.h>
#include "../opp/opp.h"

struct per_device_qos {
	struct regulator *regulator;
	struct freq_qos_request qos;
};

static DEFINE_MUTEX(regulator_mutex);
static struct notifier_block vol_constraints_notifier;
static struct freq_constraints vol_constraints;
static struct per_device_qos *vol_qos[CONFIG_NR_CPUS];

#ifdef CONFIG_CPU_HOTPLUG_THERMAL
struct thermal_cooling_device **ghotplug_cooling;
extern struct thermal_cooling_device **
of_hotplug_cooling_register(struct cpufreq_policy *policy);
#endif

static int spacemit_vol_qos_notifier_call(struct notifier_block *nb, unsigned long action, void *data)
{
	regulator_set_voltage(vol_qos[0]->regulator, action * 1000, action * 1000);

	return 0;
}

static int spacemit_policy_notifier(struct notifier_block *nb,
                                  unsigned long event, void *data)
{
	int cpu, err;
	u64 rates;
	static int cci_init;
	struct clk *cci_clk;
	struct device *cpu_dev;
	struct cpufreq_policy *policy = data;
	struct opp_table *opp_table;
	const char *strings;

	cpu = cpumask_first(policy->related_cpus);
	cpu_dev = get_cpu_device(cpu);
	opp_table = _find_opp_table(cpu_dev);

	if (cci_init == 0) {
		cci_clk = of_clk_get_by_name(opp_table->np, "cci");
		of_property_read_u64_array(opp_table->np, "cci-hz", &rates, 1);
		clk_set_rate(cci_clk, rates);
		clk_put(cci_clk);
		cci_init = 1;
	}

	vol_qos[cpu] = devm_kzalloc(cpu_dev, sizeof(struct per_device_qos), GFP_KERNEL);
	if (!vol_qos[cpu])
		return -ENOMEM;

	err = of_property_read_string_array(cpu_dev->of_node, "vin-supply-names",
			&strings, 1);
	if (err >= 0) {
		vol_qos[cpu]->regulator = devm_regulator_get(cpu_dev, strings);
		if (IS_ERR(vol_qos[cpu]->regulator)) {
			pr_err("regulator supply %s, get failed\n", strings);
			return PTR_ERR(vol_qos[cpu]->regulator);
		}

		err = regulator_enable(vol_qos[cpu]->regulator);

	} else {
		/* using the same regulator */
		vol_qos[cpu]->regulator = vol_qos[0]->regulator;
	}

	if (vol_qos[cpu]->regulator)
		freq_qos_add_request(&vol_constraints, &vol_qos[cpu]->qos, FREQ_QOS_MIN,
				regulator_get_voltage(vol_qos[cpu]->regulator) / 1000);

#ifdef CONFIG_CPU_HOTPLUG_THERMAL
       ghotplug_cooling = of_hotplug_cooling_register(policy);
       if (!ghotplug_cooling) {
               pr_err("register hotplug cpu cooling failed\n");
               return -EINVAL;
       }
#endif
	return 0;
}

static int spacemit_processor_notifier(struct notifier_block *nb,
                                  unsigned long event, void *data)
{
	int cpu;
	struct device *cpu_dev;
	struct cpufreq_freqs *freqs = (struct cpufreq_freqs *)data;
	struct cpufreq_policy *policy = ( struct cpufreq_policy *)freqs->policy;
	struct opp_table *opp_table;
	struct device_node *np;
	struct clk *tcm_clk, *ace_clk;
	u64 rates;
	u32 microvol;

	cpu = cpumask_first(policy->related_cpus);
	cpu_dev = get_cpu_device(cpu);
	opp_table = _find_opp_table(cpu_dev);

	for_each_available_child_of_node(opp_table->np, np) {
		of_property_read_u64_array(np, "opp-hz", &rates, 1);
		if (rates == freqs->new * 1000) {
			of_property_read_u32(np, "opp-microvolt", &microvol);
			break;
		}
	}

	/* get the tcm/ace clk handler */
	tcm_clk = of_clk_get_by_name(opp_table->np, "tcm");
	ace_clk = of_clk_get_by_name(opp_table->np, "ace");

	if (event == CPUFREQ_PRECHANGE) {

		mutex_lock(&regulator_mutex);

		if (freqs->new > freqs->old) {
			/* increase voltage first */
			if (vol_qos[cpu]->regulator)
				freq_qos_update_request(&vol_qos[cpu]->qos, microvol / 1000);
		}

		/**
		 * change the tcm/ace's frequency first.
		 * binary division is safe
		 */
		if (!IS_ERR(ace_clk)) {
			clk_set_rate(ace_clk, clk_get_rate(clk_get_parent(ace_clk)) / 2);
			clk_put(ace_clk);
		}

		if (!IS_ERR(tcm_clk)) {
			clk_set_rate(tcm_clk, clk_get_rate(clk_get_parent(tcm_clk)) / 2);
			clk_put(tcm_clk);
		}
	}

	if (event == CPUFREQ_POSTCHANGE) {

		if (!IS_ERR(tcm_clk)) {
			clk_get_rate(clk_get_parent(tcm_clk));
			/* get the tcm-hz */
			of_property_read_u64_array(np, "tcm-hz", &rates, 1);
			/* then set rate */
			clk_set_rate(tcm_clk, rates);
			clk_put(tcm_clk);
		}

		if (!IS_ERR(ace_clk)) {
			clk_get_rate(clk_get_parent(ace_clk));
			/* get the ace-hz */
			of_property_read_u64_array(np, "ace-hz", &rates, 1);
			/* then set rate */
			clk_set_rate(ace_clk, rates);
			clk_put(ace_clk);
		}

		if (freqs->new < freqs->old) {
			/* decrease the voltage last */
			if (vol_qos[cpu]->regulator)
				freq_qos_update_request(&vol_qos[cpu]->qos, microvol / 1000);
		}

		mutex_unlock(&regulator_mutex);
	}

	dev_pm_opp_put_opp_table(opp_table);

	return 0;
}

static struct notifier_block spacemit_processor_notifier_block = {
       .notifier_call = spacemit_processor_notifier,
};

static struct notifier_block spacemit_policy_notifier_block = {
       .notifier_call = spacemit_policy_notifier,
};

static int __init spacemit_processor_driver_init(void)
{
       int ret;

       ret = cpufreq_register_notifier(&spacemit_processor_notifier_block, CPUFREQ_TRANSITION_NOTIFIER);
       if (ret) {
               pr_err("register cpufreq notifier failed\n");
               return -EINVAL;
       }

       ret = cpufreq_register_notifier(&spacemit_policy_notifier_block, CPUFREQ_POLICY_NOTIFIER);
       if (ret) {
               pr_err("register cpufreq notifier failed\n");
               return -EINVAL;
       }

       vol_constraints_notifier.notifier_call = spacemit_vol_qos_notifier_call;
       freq_constraints_init(&vol_constraints);
       freq_qos_add_notifier(&vol_constraints, FREQ_QOS_MIN, &vol_constraints_notifier);

       return 0;
}

arch_initcall(spacemit_processor_driver_init);
