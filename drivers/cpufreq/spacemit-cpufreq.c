#include <linux/cpufreq.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/cpumask.h>
#include <linux/clk/clk-conf.h>
#include <linux/pm_qos.h>
#include <linux/notifier.h>
#include "../opp/opp.h"

#if 0
#define CCI_CLK_DEFAULT_RATE	245760

struct per_dev_qos {
	struct clk *clk;
	struct notifier_block notifier;
	struct dev_pm_qos_request req;
};

static struct per_dev_qos *clk_dev_qos;

static int spacemit_handle_clk_max_notifier_call(struct notifier_block *nb, unsigned long action, void *data)
{
	struct per_dev_qos *per_qos = container_of(nb, struct per_dev_qos, notifier);

	clk_set_rate(per_qos->clk, action * 1000);

	return 0;
}
#endif

static int spacemit_policy_notifier(struct notifier_block *nb,
                                  unsigned long event, void *data)
{
	int cpu;
	u64 rates;
	static int cci_init;
	struct clk *cci_clk;
	struct device *cpu_dev;
	struct cpufreq_policy *policy = data;
	struct opp_table *opp_table;

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

	cpu = cpumask_first(policy->related_cpus);
	cpu_dev = get_cpu_device(cpu);
	opp_table = _find_opp_table(cpu_dev);

#if 0
	if (clk_dev_qos == NULL) {
		clk_dev_qos = (struct per_dev_qos *)devm_kzalloc(cpu_dev, sizeof(struct per_dev_qos), GFP_KERNEL);
		if (!clk_dev_qos) {
			pr_err(" allocate per device qos error\n");
			return -ENOMEM;
		}

		clk_dev_qos->clk = of_clk_get_by_name(opp_table->np, "cci");
		clk_dev_qos->notifier.notifier_call = spacemit_handle_clk_max_notifier_call;
		dev_pm_qos_add_notifier(get_cpu_device(0), &clk_dev_qos->notifier, DEV_PM_QOS_MAX_FREQUENCY);
		dev_pm_qos_add_request(get_cpu_device(0), &clk_dev_qos->req, DEV_PM_QOS_MAX_FREQUENCY, CCI_CLK_DEFAULT_RATE);
	}
#endif
	for_each_available_child_of_node(opp_table->np, np) {
		of_property_read_u64_array(np, "opp-hz", &rates, 1);
		if (rates == freqs->new * 1000)
			break;
	}
#if 0
	if (event == CPUFREQ_PRECHANGE) {
		/* decrease the freqs */
		if (freqs->old > freqs->new) {
			/* decrease the cci first */
			of_property_read_u64_array(np, "cci-hz", &rates, 1);
			dev_pm_qos_update_request(&clk_dev_qos->req, rates / 1000);
		}
	}
#endif

	/* get the tcm/ace clk handler */
	tcm_clk = of_clk_get_by_name(opp_table->np, "tcm");
	ace_clk = of_clk_get_by_name(opp_table->np, "ace");

	if (event == CPUFREQ_PRECHANGE) {
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
#if 0
		/* increase the freqs */
		if (freqs->old < freqs->new) {
			/* increase the cci after */
			of_property_read_u64_array(np, "cci-hz", &rates, 1);
			dev_pm_qos_update_request(&clk_dev_qos->req, rates / 1000);
		}
#endif
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

       return 0;
}

arch_initcall(spacemit_processor_driver_init);
